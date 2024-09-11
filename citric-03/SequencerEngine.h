#pragma once

#include "daisy_seed.h"
#include "daisysp.h"
#include <unordered_map>
#include <string>
#include <vector>
#include <random>
#include <chrono>

using namespace daisy;
using namespace daisysp;

class SequencerEngine {
public:
    SequencerEngine();
    void Init();
    void Process(AudioHandle::InterleavingInputBuffer in, AudioHandle::InterleavingOutputBuffer out, size_t size);

private:
    static const int STEPS = 16;
    static const int NUMBER_OF_POTS = 7;
    static const int LOW_RANGE_BPM = 30;
    static const int HIGH_RANGE_BPM = 330;
    static const int CUTOFF_MAX = 16000;
    static const int CUTOFF_MIN = 0;
    static const float DECAY_MAX;
    static const float DECAY_MIN;
    static const float MAX_RESONANCE;
    static const int FILTER_MOVEMENT = 11000;

    DaisySeed hardware;
    Oscillator osc;
    MoogLadder flt;
    Overdrive dist;
    AdEnv synthVolEnv, synthPitchEnv;
    Switch change_page, activate_slide;
    uint16_t activate_state, random_state, switch_state, slide_state;
    GPIO activate_sequence, random_sequence, switch_mode;
    AdcChannelConfig pots[NUMBER_OF_POTS];
    std::vector<GPIO> seq_buttons;
    Metro tick;
    GPIO page_led;
    GPIO led_decoder_out1, led_decoder_out2, led_decoder_out3;

    int active_step;
    int mode_int;
    int selected_note;
    int page_adder;
    float env_mod;
    float cutoff;
    float tempo_bpm;
    std::string mode;
    bool active;
    bool current_note;

    std::unordered_map<std::string, std::vector<double>> notes;
    std::vector<std::string> all_notes;
    std::vector<std::string> scale;
    std::vector<std::string> sequence;
    std::vector<bool> slide;
    std::vector<bool> activated_notes;

    uint16_t last_button_states[8];
    std::vector<int> counters;

    std::chrono::high_resolution_clock::time_point time_at_boot;
    std::random_device rd;

    void setPitch(double freq);
    void setSlide(double note, double note_before);
    std::string circularShiftLeft(std::string mode);
    std::vector<std::string> circularShiftLeftArray(std::vector<std::string> array);
    std::vector<std::string> generateScale();
    std::mt19937 generateRandomEngine();
    std::vector<std::string> randomizeSequence();
    float convertBPMtoFreq(float bpm);
    void increasePitchForActiveNote();
    bool debounce_shift(GPIO &button, uint16_t &state);
    void handleSequenceButtons();
    void inputHandler();
    void prepareAudioBlock(size_t size, AudioHandle::InterleavingOutputBuffer out);
    double getFreqOfNote(std::string note);
    int modulo(int dividend, int divisor);
    void triggerSequence();
    void configureAndInitHardware();
    void initOscillator(float samplerate);
    void initPitchEnv(float samplerate);
    void initVolEnv(float samplerate);
    void initButtons(float samplerate);
    void initPots();
    void initFilter(float samplerate);
    void initTick(float samplerate);
    void initSeqButtons();
    void playSequence(size_t size, AudioHandle::InterleavingOutputBuffer out);
};