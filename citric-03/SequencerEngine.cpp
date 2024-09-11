#include "SequencerEngine.h"

const float SequencerEngine::DECAY_MAX = 0.9f;
const float SequencerEngine::DECAY_MIN = 0.01f;
const float SequencerEngine::MAX_RESONANCE = 0.89f;

SequencerEngine::SequencerEngine()
    : active_step(0), mode_int(0), selected_note(0), page_adder(0),
      env_mod(0.8f), cutoff(13000.f), tempo_bpm(120.f),
      mode("HWWHWWW"), active(false), current_note(true),
      time_at_boot(std::chrono::high_resolution_clock::now()) {
    
    notes = {
        {"C", {16.35, 32.70, 65.41, 130.81, 261.63, 523.25, 1046.50, 2093.00, 4186.01}},
        {"Db", {17.32, 34.65, 69.30, 138.59, 277.18, 554.37, 1108.73, 2217.46, 4434.92}},
        {"D", {18.35, 36.71, 73.42, 146.83, 293.66, 587.33, 1174.66, 2349.32, 4698.64}},
        {"Eb", {19.45, 38.89, 77.78, 155.56, 311.13, 622.25, 1244.51, 2489.02, 4978.03}},
        {"E", {20.60, 41.20, 82.41, 164.81, 329.63, 659.26, 1318.51, 2637.02}},
        {"F", {21.83, 43.65, 87.31, 174.61, 349.23, 698.46, 1396.91, 2793.83}},
        {"Gb", {23.12, 46.25, 92.50, 185.00, 369.99, 739.99, 1479.98, 2959.96}},
        {"G", {24.50, 49.00, 98.00, 196.00, 392.00, 783.99, 1567.98, 3135.96}},
        {"Ab", {25.96, 51.91, 103.83, 207.65, 415.30, 830.61, 1661.22, 3322.44}},
        {"A", {27.50, 55.00, 110.00, 220.00, 440.00, 880.00, 1760.00, 3520.00}},
        {"Bb", {29.14, 58.27, 116.54, 233.08, 466.16, 932.33, 1864.66, 3729.31}},
        {"B", {30.87, 61.74, 123.47, 246.94, 493.88, 987.77, 1975.53, 3951.07}}
    };

    all_notes = {"C","Db","D","Eb","E","F","Gb","G","Ab","A","Bb","B","C2"};
    scale = all_notes;
    sequence.resize(STEPS, scale[0]);
    slide.resize(STEPS, false);
    activated_notes.resize(STEPS, true);
    counters.resize(8, 0);
}

void SequencerEngine::Init() {
    configureAndInitHardware();
    
    float samplerate = hardware.AudioSampleRate();
    
    initOscillator(samplerate);
    initPitchEnv(samplerate);
    initVolEnv(samplerate);
    initButtons(samplerate);
    initPots();
    initFilter(samplerate);
    initTick(samplerate);
    initSeqButtons();
    dist.SetDrive(0.5);
    
    led_decoder_out1.Init(daisy::seed::D12, GPIO::Mode::OUTPUT);
    led_decoder_out2.Init(daisy::seed::D13, GPIO::Mode::OUTPUT);
    led_decoder_out3.Init(daisy::seed::D14, GPIO::Mode::OUTPUT);
    page_led.Init(daisy::seed::D23, GPIO::Mode::OUTPUT);

    GPIO demux;
    demux.Init(daisy::seed::D22, GPIO::Mode::OUTPUT);
    demux.Write(false);

    hardware.adc.Start();
}

void SequencerEngine::Process(AudioHandle::InterleavingInputBuffer in, AudioHandle::InterleavingOutputBuffer out, size_t size) {
    inputHandler();
    playSequence(size, out);
}

void SequencerEngine::setPitch(double freq) {
    synthPitchEnv.SetMax(freq);
    synthPitchEnv.SetMin(freq);
}

void SequencerEngine::setSlide(double note, double note_before) {
    synthPitchEnv.SetMax(note);
    synthPitchEnv.SetMin(note_before);
    synthPitchEnv.SetTime(ADENV_SEG_DECAY, static_cast<float>(60/tempo_bpm));
}

std::string SequencerEngine::circularShiftLeft(std::string mode) {
    char first = mode[0];
    mode.erase(0, 1);
    return mode += first;
}

std::vector<std::string> SequencerEngine::circularShiftLeftArray(std::vector<std::string> array) {
    std::vector<std::string> new_array(array);
    std::rotate(new_array.begin(), new_array.begin() + 1, new_array.end());
    return new_array;
}

std::vector<std::string> SequencerEngine::generateScale() {
    std::vector<std::string> new_scale(8);

    int index = 0;
    size_t notes_collected = 0;
    while (notes_collected < 8) {
        new_scale[notes_collected] = all_notes[index % all_notes.size()];
        index += (mode[notes_collected] == 'W') ? 2 : 1;
        notes_collected++;
    }  
    return new_scale;      
}

std::mt19937 SequencerEngine::generateRandomEngine() {
    auto current_time = std::chrono::high_resolution_clock::now();
    unsigned seed = static_cast<unsigned>(std::chrono::high_resolution_clock::duration(time_at_boot - current_time).count() ^ rd());
    return std::mt19937(seed);
}

std::vector<std::string> SequencerEngine::randomizeSequence() {
    std::mt19937 rng = generateRandomEngine();
    std::vector<std::string> resulting_sequence(sequence.size());

    for(int i = 0; i < static_cast<int>(resulting_sequence.size()); i++) {    
        std::uniform_int_distribution<unsigned> distrib(0, scale.size() - 1);
        int randomIndex = distrib(rng);
        resulting_sequence[i] = scale[randomIndex];
    }

    return resulting_sequence;
}

float SequencerEngine::convertBPMtoFreq(float bpm) {
    return (bpm / 60.f) * 8.f;
}

void SequencerEngine::increasePitchForActiveNote() {
    for(int i = 0; i < 8; i++) {
        if(sequence[selected_note] == scale[i] && i == 7) {
            sequence[selected_note] = scale[1];
            break;
        }
        else if (sequence[selected_note] == scale[i]) {
            sequence[selected_note] = scale[i+1];
            break;
        }
    }
}

bool SequencerEngine::debounce_shift(GPIO &button, uint16_t &state) {
    state = (state << 1) | button.Read() | 0xfe00;
    return (state == 0xff00);
}

void SequencerEngine::handleSequenceButtons() {
    for(int i = 0; i < 8; i++) {
        if(debounce_shift(seq_buttons[i], last_button_states[i])) {
            if(!activate_slide.Pressed())
                slide[i + page_adder] = !slide[i + page_adder];
            else {
                int pitch = hardware.adc.GetFloat(3) * scale.size();
                if(pitch == 0)
                    activated_notes[i + page_adder] = !activated_notes[i + page_adder];
                else {
                    sequence[i + page_adder] = scale[pitch];
                    activated_notes[i + page_adder] = true;
                }
            }
        }
    }
}

void SequencerEngine::inputHandler() {
    activate_slide.Debounce();
    change_page.Debounce();
    
    if(debounce_shift(activate_sequence, activate_state))
        active = !active;

    if(change_page.RisingEdge()) {
        page_adder = (page_adder + 8) % 16;
        page_led.Write(page_adder);
    }

    if(debounce_shift(random_sequence, random_state)) {
        active_step = 0;
        sequence = randomizeSequence();
    }
    
    if(debounce_shift(switch_mode, switch_state)) {
        if(mode_int == 7) mode_int = 0;
        else mode_int++;

        if(mode_int != 0) {
            mode = circularShiftLeft(mode);
            scale = generateScale();
        }
        else scale = all_notes;

        for(size_t i = 0; i < STEPS; i++)
            sequence[i] = scale[i % scale.size()];
    }

    handleSequenceButtons();

    tempo_bpm = floor((hardware.adc.GetFloat(0) * (HIGH_RANGE_BPM - LOW_RANGE_BPM)) + LOW_RANGE_BPM);
    tick.SetFreq(convertBPMtoFreq(tempo_bpm));
    
    cutoff = hardware.adc.GetFloat(1) * (CUTOFF_MAX - CUTOFF_MIN) + CUTOFF_MIN;

    float resonance = hardware.adc.GetFloat(2) * (MAX_RESONANCE);
    flt.SetRes(resonance);

    float decay = hardware.adc.GetFloat(4) * (DECAY_MAX - DECAY_MIN) + DECAY_MIN;
    synthVolEnv.SetTime(ADENV_SEG_DECAY, decay);
    
    env_mod = hardware.adc.GetFloat(5) * 1.0;
    dist.SetDrive(hardware.adc.GetFloat(6) * 0.7);
}

void SequencerEngine::prepareAudioBlock(size_t size, AudioHandle::InterleavingOutputBuffer out) {
    float osc_out, synth_env_out, sig;
    for(size_t i = 0; i < size; i += 2) {
        synth_env_out = synthVolEnv.Process();
        osc.SetFreq(synthPitchEnv.Process());
        osc.SetAmp(synth_env_out);
        osc_out = osc.Process();
        
        flt.SetFreq(env_mod * synth_env_out * FILTER_MOVEMENT + cutoff);
        
        sig = dist.Process(flt.Process(osc_out));

        out[i] = sig;
        out[i + 1] = sig;
    }
}

double SequencerEngine::getFreqOfNote(std::string note) {
    double current_freq;
    if(note == "C2")
        current_freq = notes[note.substr(0,1)][3];
    else
        current_freq = notes[note][2];

    return current_freq;
}

int SequencerEngine::modulo(int dividend, int divisor) {
    return (dividend % divisor + divisor) % divisor;
}

void SequencerEngine::triggerSequence() {
    if(tick.Process()) {
        bool current_page = !((active_step >> 3) ^ (page_adder >> 3));
        led_decoder_out1.Write(current_page * (active_step & 0x1));
        led_decoder_out2.Write(current_page * (active_step & 0x2));
        led_decoder_out3.Write(current_page * (active_step & 0x4));

        std::string note = sequence[active_step];
        double current_freq = getFreqOfNote(note);
        
        if(slide[active_step]) {
            double previous_freq = getFreqOfNote(sequence[modulo((active_step - 1), STEPS)]); 
            setSlide(current_freq, previous_freq);
        }
        else
            setPitch(current_freq);
        synthVolEnv.Trigger();
        synthPitchEnv.Trigger();
        
        active_step = (active_step + 1) % STEPS;
        current_note = activated_notes[active_step];
    }
}

void SequencerEngine::configureAndInitHardware() {
    hardware.Configure();
    hardware.Init();
    hardware.SetAudioBlockSize(4);
}

void SequencerEngine::initOscillator(float samplerate) {
    osc.Init(samplerate);
    osc.SetWaveform(Oscillator::WAVE_SAW);
    osc.SetAmp(1);
}

void SequencerEngine::initPitchEnv(float samplerate) {
    synthPitchEnv.Init(samplerate);
    synthPitchEnv.SetTime(ADENV_SEG_ATTACK, .01);
    synthPitchEnv.SetTime(ADENV_SEG_DECAY, .05);
    synthPitchEnv.SetMax(400);
    synthPitchEnv.SetMin(400);
}

void SequencerEngine::initVolEnv(float samplerate) {
    synthVolEnv.Init(samplerate);
    synthVolEnv.SetTime(ADENV_SEG_ATTACK, .01);
    synthVolEnv.SetTime(ADENV_SEG_DECAY, 1);
    synthVolEnv.SetMax(1);
    synthVolEnv.SetMin(0);
}

void SequencerEngine::initButtons(float samplerate) {
    activate_sequence.Init(daisy::seed::D9, GPIO::Mode::INPUT, GPIO::Pull::PULLDOWN);
    random_sequence.Init(daisy::seed::D10, GPIO::Mode::INPUT, GPIO::Pull::PULLDOWN);
    switch_mode.Init(daisy::seed::D11, GPIO::Mode::INPUT, GPIO::Pull::PULLDOWN);
    activate_slide.Init(hardware.GetPin(15), samplerate / 48.f);
    change_page.Init(hardware.GetPin(24), samplerate / 48.f);
}

void SequencerEngine::initPots() {
    pots[0].InitSingle(hardware.GetPin(16));
    pots[1].InitSingle(hardware.GetPin(17));
    pots[2].InitSingle(hardware.GetPin(18));
    pots[3].InitSingle(hardware.GetPin(21));
    pots[4].InitSingle(hardware.GetPin(20));
    pots[5].InitSingle(hardware.GetPin(19));
    pots[6].InitSingle(hardware.GetPin(28));
    hardware.adc.Init(pots, NUMBER_OF_POTS);
}

void SequencerEngine::initFilter(float samplerate) {
    flt.Init(samplerate);
    flt.SetRes(0.7);
    flt.SetFreq(700);
}

void SequencerEngine::initTick(float samplerate) {
    tick.Init((tempo_bpm / 60.f) * 4.f, samplerate);
}

void SequencerEngine::initSeqButtons() {
    seq_buttons.resize(8);
    seq_buttons[0].Init(daisy::seed::D1, GPIO::Mode::INPUT, GPIO::Pull::NOPULL);
    seq_buttons[1].Init(daisy::seed::D2, GPIO::Mode::INPUT, GPIO::Pull::NOPULL);
    seq_buttons[2].Init(daisy::seed::D3, GPIO::Mode::INPUT, GPIO::Pull::NOPULL);
    seq_buttons[3].Init(daisy::seed::D4, GPIO::Mode::INPUT, GPIO::Pull::NOPULL);
    seq_buttons[4].Init(daisy::seed::D6, GPIO::Mode::INPUT, GPIO::Pull::NOPULL);
    seq_buttons[5].Init(daisy::seed::D5, GPIO::Mode::INPUT, GPIO::Pull::NOPULL);
    seq_buttons[6].Init(daisy::seed::D7, GPIO::Mode::INPUT, GPIO::Pull::NOPULL);
    seq_buttons[7].Init(daisy::seed::D8, GPIO::Mode::INPUT, GPIO::Pull::NOPULL);
}

void SequencerEngine::playSequence(size_t size, AudioHandle::InterleavingOutputBuffer out) {
    if(active) {
        prepareAudioBlock(size, out);
        if(current_note)
            triggerSequence();
        else if (tick.Process()) {
            active_step = (active_step + 1) % STEPS;
            current_note = activated_notes[active_step];
        }
    }
    else {
        for(size_t i = 0; i < size; i += 2) {
            out[i] = out[i] * 0.9;
            out[i + 1] = out[i] * 0.9;
        }
    }
}