#ifndef IMP_BACKCHANNEL_HPP
#define IMP_BACKCHANNEL_HPP

// Define the list of backchannel formats and their properties
// X(EnumName, NameString, PayloadType, Frequency, MimeType)
#define X_FOREACH_BACKCHANNEL_FORMAT(X) \
    X(OPUS, "OPUS", 96, 48000, "audio/OPUS") \
    X(PCMU, "PCMU", 0, 8000, "audio/PCMU") \
    X(PCMA, "PCMA", 8, 8000, "audio/PCMA") \
    /* Add new formats here */

#define APPLY_ENUM(EnumName, NameString, PayloadType, Frequency, MimeType) EnumName,
enum class IMPBackchannelFormat {
    UNKNOWN = -1,
    X_FOREACH_BACKCHANNEL_FORMAT(APPLY_ENUM)
};
#undef APPLY_ENUM

class IMPBackchannel {
public:
    static IMPBackchannel* createNew();
    IMPBackchannel();
    ~IMPBackchannel();

    int init();
    void deinit();
};

#endif // IMP_BACKCHANNEL_HPP
