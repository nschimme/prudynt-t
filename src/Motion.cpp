#include "Motion.hpp"

using namespace std::chrono;
bool ignoreInitialPeriod = true;

std::string Motion::getConfigPath(const char *itemName)
{
    return "motion." + std::string(itemName);
}

void Motion::detect()
{
    LOG_INFO("Start motion detection thread.");

    int ret;
    int debounce = 0;
    IMP_IVS_MoveOutput *result;
    bool isInCooldown = false;
    auto cooldownEndTime = steady_clock::now();
    auto motionEndTime = steady_clock::now();
    auto startTime = steady_clock::now();

    if(init() != 0) return;

    global_motion_thread_signal = true;
    while (global_motion_thread_signal)
    {

        ret = IMP_IVS_PollingResult(ivsChn, cfg->motion.ivs_polling_timeout);
        if (ret < 0)
        {
            LOG_WARN("IMP_IVS_PollingResult error: " << ret);
            continue;
        }

        ret = IMP_IVS_GetResult(ivsChn, (void **)&result);
        if (ret < 0)
        {
            LOG_WARN("IMP_IVS_GetResult error: " << ret);
            continue;
        }

        auto currentTime = steady_clock::now();
        auto elapsedTime = duration_cast<seconds>(currentTime - startTime);

        if (ignoreInitialPeriod && elapsedTime.count() < cfg->motion.init_time)
        {
            continue;
        }
        else
        {
            ignoreInitialPeriod = false;
        }

        if (isInCooldown && duration_cast<seconds>(currentTime - cooldownEndTime).count() < cfg->motion.cooldown_time)
        {
            continue;
        }
        else
        {
            isInCooldown = false;
        }

        bool motionDetected = false;
        for (int i = 0; i < IMP_IVS_MOVE_MAX_ROI_CNT; i++)
        {
            if (result->retRoi[i])
            {
                motionDetected = true;
                LOG_INFO("Active motion detected in region " << i);
                debounce++;
                if (debounce >= cfg->motion.debounce_time)
                {
                    if (!moving.load())
                    {
                        moving = true;
                        LOG_INFO("Motion Start");

                        char cmd[128];
                        memset(cmd, 0, sizeof(cmd));
                        snprintf(cmd, sizeof(cmd), "%s start", cfg->motion.script_path);
                        ret = system(cmd);
                        if (ret != 0)
                        {
                            LOG_ERROR("Motion script failed:" << cmd);
                        }
                    }
                    indicator = true;
                    motionEndTime = steady_clock::now(); // Update last motion time
                }
            }
        }

        if (!motionDetected)
        {
            debounce = 0;
            auto duration = duration_cast<seconds>(currentTime - motionEndTime).count();
            if (moving && duration >= cfg->motion.min_time && duration >= cfg->motion.post_time)
            {
                LOG_INFO("End of Motion");
                char cmd[128];
                memset(cmd, 0, sizeof(cmd));
                snprintf(cmd, sizeof(cmd), "%s stop", cfg->motion.script_path);
                ret = system(cmd);
                if (ret != 0)
                {
                    LOG_ERROR("Motion script failed:" << cmd);
                }
                moving = false;
                indicator = false;
                cooldownEndTime = steady_clock::now(); // Start cooldown
                isInCooldown = true;
            }
        }

        ret = IMP_IVS_ReleaseResult(ivsChn, (void *)result);
        if (ret < 0)
        {
            LOG_WARN("IMP_IVS_ReleaseResult error: " << ret);
            continue;
        }
    }

    exit();

    LOG_DEBUG("Exit motion detect thread.");
}

int Motion::init()
{
    LOG_INFO("Initialize motion detection.");

    if((cfg->motion.monitor_stream == 0 && !cfg->stream0.enabled) || 
       (cfg->motion.monitor_stream == 1 && !cfg->stream1.enabled)) {

        LOG_ERROR("Monitor stream is disabled, abort.");
        return -1;
    }
    int ret;

    ret = IMP_IVS_CreateGroup(0);
    LOG_DEBUG_OR_ERROR_AND_EXIT(ret, "IMP_IVS_CreateGroup(0)");

    //automatically set frame size / height 
    ret = IMP_Encoder_GetChnAttr(cfg->motion.monitor_stream, &channelAttributes);
    if (ret == 0)
    {
        if (cfg->motion.frame_width == IVS_AUTO_VALUE)
        {
            cfg->set<int>(getConfigPath("frame_width"), channelAttributes.encAttr.picWidth, true);
        }
        if (cfg->motion.frame_height == IVS_AUTO_VALUE)
        {
            cfg->set<int>(getConfigPath("frame_height"), channelAttributes.encAttr.picHeight, true);
        }
        if (cfg->motion.roi_1_x == IVS_AUTO_VALUE)
        {
            cfg->set<int>(getConfigPath("roi_1_x"), channelAttributes.encAttr.picWidth - 1, true);
        }
        if (cfg->motion.roi_1_y == IVS_AUTO_VALUE)
        {
            cfg->set<int>(getConfigPath("roi_1_y"), channelAttributes.encAttr.picHeight - 1, true);
        }        
    }

    memset(&move_param, 0, sizeof(IMP_IVS_MoveParam));
    // OSD is affecting motion for some reason.
    // Sensitivity range is 0-4
    move_param.sense[0] = cfg->motion.sensitivity;
    move_param.skipFrameCnt = cfg->motion.skip_frame_count;
    move_param.frameInfo.width = cfg->motion.frame_width;
    move_param.frameInfo.height = cfg->motion.frame_height;

    LOG_INFO("Motion detection:" << 
             " sensibility: " << move_param.sense[0] << 
             ", skipCnt:" << move_param.skipFrameCnt << 
             ", width:" << move_param.frameInfo.width << 
             ", height:" << move_param.frameInfo.height);

    // Validate grid dimensions from config
    if (cfg->motion.grid_cols <= 0 || cfg->motion.grid_rows <= 0) {
        LOG_ERROR("Motion grid dimensions (cols=" << cfg->motion.grid_cols << ", rows=" << cfg->motion.grid_rows << ") must be positive.");
        return -1; // Or handle error appropriately
    }

    // Calculate cell dimensions based on frame size and grid config
    int cell_width = move_param.frameInfo.width / cfg->motion.grid_cols;
    int cell_height = move_param.frameInfo.height / cfg->motion.grid_rows;
    if (cell_width <= 0 || cell_height <= 0) {
         LOG_ERROR("Calculated motion cell dimensions are invalid (" << cell_width << "x" << cell_height << "). Check frame ("
                   << move_param.frameInfo.width << "x" << move_param.frameInfo.height << ") and grid ("
                   << cfg->motion.grid_cols << "x" << cfg->motion.grid_rows << ") dimensions.");
         return -1;
    }

    // Iterate through grid cells and populate active ROIs based on the roi_mask vector
    int active_roi_count = 0;
    // uint64_t mask = cfg->motion.active_cell_mask; // Removed mask usage
    int total_cells = cfg->motion.grid_cols * cfg->motion.grid_rows;

    // Determine the actual hardware limit
    const int max_roi_limit = IMP_IVS_MOVE_MAX_ROI_CNT;
    LOG_INFO("Motion grid configured to " << cfg->motion.grid_cols << "x" << cfg->motion.grid_rows << " (" << total_cells << " cells). Hardware ROI limit: " << max_roi_limit);

    for (int cell_index = 0; cell_index < total_cells; ++cell_index) {
        // Check if the index is valid and the corresponding boolean is true in the roi_mask vector
        if (cell_index < cfg->motion.roi_mask.size() && cfg->motion.roi_mask[cell_index]) {
            // Check if we have reached the hardware limit
            if (active_roi_count >= max_roi_limit) {
                LOG_WARN("Hardware ROI limit (" << max_roi_limit << ") reached. Ignoring remaining active grid cells.");
                break; // Stop adding ROIs
            }

            int row = cell_index / cfg->motion.grid_cols;
            int col = cell_index % cfg->motion.grid_cols;

            // Calculate coordinates based on cell_width, cell_height, row, col
            int p0x = col * cell_width;
            int p0y = row * cell_height;
            // Calculate end coordinates, clamp to frame boundaries, ensure p1 >= p0
            int p1x = std::min((col + 1) * cell_width, (int)move_param.frameInfo.width) - 1;
            int p1y = std::min((row + 1) * cell_height, (int)move_param.frameInfo.height) - 1;
            p1x = std::max(p0x, p1x); // Ensure p1.x >= p0.x
            p1y = std::max(p0y, p1y); // Ensure p1.y >= p0.y

            move_param.roiRect[active_roi_count].p0.x = p0x;
            move_param.roiRect[active_roi_count].p0.y = p0y;
            move_param.roiRect[active_roi_count].p1.x = p1x;
            move_param.roiRect[active_roi_count].p1.y = p1y;

            LOG_INFO("Adding active motion ROI " << active_roi_count << " for grid cell (" << col << "," << row << "): p0("
                     << p0x << "," << p0y << "), p1(" << p1x << "," << p1y << ")");

            active_roi_count++;
        }
    }

    move_param.roiRectCnt = active_roi_count; // Set the count of ROIs actually added
    LOG_INFO("Total active motion ROIs configured for hardware: " << active_roi_count);


    // Create the IVS interface with the populated parameters
    move_intf = IMP_IVS_CreateMoveInterface(&move_param);
    if (move_intf == nullptr) {
        LOG_ERROR("Failed to create IVS Move Interface.");
        return -1;
    }

    ret = IMP_IVS_CreateChn(ivsChn, move_intf);
    LOG_DEBUG_OR_ERROR_AND_EXIT(ret, "IMP_IVS_CreateChn(" << ivsChn << ", move_intf)");

    ret = IMP_IVS_RegisterChn(ivsGrp, ivsChn);
    LOG_DEBUG_OR_ERROR_AND_EXIT(ret, "IMP_IVS_RegisterChn(" << ivsGrp << ", " << ivsChn << ")");

    ret = IMP_IVS_StartRecvPic(ivsChn);
    LOG_DEBUG_OR_ERROR_AND_EXIT(ret, "IMP_IVS_StartRecvPic(" << ivsChn << ")")

    fs = { 
        /**< Device ID */ DEV_ID_FS, 
        /**< Group ID */  cfg->motion.monitor_stream, 
        /**< output ID */ 1 
    };

    ivs_cell = { 
        /**< Device ID */ DEV_ID_IVS, 
        /**< Group ID */  0, 
        /**< output ID */ 0 
    };

    ret = IMP_System_Bind(&fs, &ivs_cell);
    LOG_DEBUG_OR_ERROR_AND_EXIT(ret, "IMP_System_Bind(&fs, &ivs_cell)");

    return ret;
}

int Motion::exit()
{
    int ret;

    LOG_DEBUG("Exit motion detection.");

    ret = IMP_IVS_StopRecvPic(ivsChn);
    LOG_DEBUG_OR_ERROR(ret, "IMP_IVS_StopRecvPic(0)");

    ret = IMP_System_UnBind(&fs, &ivs_cell);
    LOG_DEBUG_OR_ERROR_AND_EXIT(ret, "IMP_System_UnBind(&fs, &ivs_cell)");

    ret = IMP_IVS_UnRegisterChn(ivsChn);
    LOG_DEBUG_OR_ERROR(ret, "IMP_IVS_UnRegisterChn(0)");

    ret = IMP_IVS_DestroyChn(ivsChn);
    LOG_DEBUG_OR_ERROR(ret, "IMP_IVS_DestroyChn(0)");

    ret = IMP_IVS_DestroyGroup(ivsGrp);
    LOG_DEBUG_OR_ERROR(ret, "IMP_IVS_DestroyGroup(0)");

    IMP_IVS_DestroyMoveInterface(move_intf);

    return ret;
}

void *Motion::run(void *arg)
{
    ((Motion *)arg)->detect();
    return nullptr;
}
