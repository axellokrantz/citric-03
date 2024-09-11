#include "SequencerEngine.h"

SequencerEngine sequencer;

void AudioCallback(AudioHandle::InterleavingInputBuffer in, AudioHandle::InterleavingOutputBuffer out, size_t size) {
    sequencer.Process(in, out, size);
}

int main(void) {
    sequencer.Init();
    sequencer.hardware.StartAudio(AudioCallback);
    
    while(true) {}

    return 0;
}