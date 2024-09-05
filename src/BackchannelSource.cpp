#include "PCMBackchannelSource.hpp"

PCMBackchannelSource* PCMBackchannelSource::createNew(UsageEnvironment& env) {
    return new PCMBackchannelSource(env);
}

PCMBackchannelSource::PCMBackchannelSource(UsageEnvironment& env)
    : FramedSource(env) {
    // Constructor implementation
}

PCMBackchannelSource::~PCMBackchannelSource() {
    // Destructor implementation
}

void PCMBackchannelSource::doGetNextFrame() {
    // TODO: Implement the logic to retrieve and process PCM audio frames for backchannel streaming.
    // Ensure compliance with Profile-T mandatory requirements, such as bitrate, timing, and packetization rules.

    mixAndOutputToSpeaker();

    // After processing, invoke 'afterGetting(this);' to continue streaming.
    afterGetting(this);
}

void PCMBackchannelSource::addClientStream(const u_int8_t* data, unsigned dataSize) {
    std::lock_guard<std::mutex> lock(streamMutex);
    clientStreams.push_back({data, dataSize});
}

void PCMBackchannelSource::removeClientStream(unsigned clientId) {
    std::lock_guard<std::mutex> lock(streamMutex);
    if (clientId < clientStreams.size()) {
        clientStreams.erase(clientStreams.begin() + clientId);
    }
}

void PCMBackchannelSource::mixAndOutputToSpeaker() {
    std::lock_guard<std::mutex> lock(streamMutex);
    
    // TODO: Implement mixing logic to combine multiple client streams into one.
    // For simplicity, a basic mix might involve averaging the audio samples, but more advanced mixing techniques could be used.

    if (clientStreams.empty()) {
        return;
    }

    // Example: Simple average mixing (placeholder)
    unsigned mixedDataSize = clientStreams[0].second; // Assume all streams have the same size
    u_int8_t* mixedData = new u_int8_t[mixedDataSize];

    // Initialize mixedData to zero
    std::fill_n(mixedData, mixedDataSize, 0);

    // Sum audio data from all clients
    for (const auto& stream : clientStreams) {
        const u_int8_t* data = stream.first;
        for (unsigned i = 0; i < mixedDataSize; ++i) {
            mixedData[i] += data[i]; // Simple sum, consider averaging or other mixing algorithms
        }
    }

    // Output the mixed data to the speaker
    outputToSpeaker(mixedData, mixedDataSize);

    delete[] mixedData;
}

void PCMBackchannelSource::outputToSpeaker(const u_int8_t* data, unsigned dataSize) {
    // TODO: Implement the actual audio output to the server's speaker.
    // This could involve using an audio library like PortAudio, ALSA, etc.
    // Example: Using PortAudio to play the PCM data
}

