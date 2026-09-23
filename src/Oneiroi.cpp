#include "plugin.hpp"

#include "Oneiroi_1_2_2Patch.hpp"
#include "basicmaths.h"
#include "FastLogTable.h"
#include "FastPowTable.h"

// Motorised fader / knob semantics: this module drives the Oneiroi DSP by
// writing PatchCtrls/PatchCvs every audio block (the firmware's knob/fader
// read loops are compiled out under VCV). The firmware UI Poll still runs
// (clock, LEDs, button state machine, recording), so buttons and lights are
// shared between the module and the Oneiroi Ui.

struct OneiroiVCV : Module {
    enum ParamId {
        // Main knobs (rows 1-3 of the panel grid)
        LOOP_SPEED_PARAM,
        LOOP_START_PARAM,
        LOOP_LENGTH_PARAM,
        OSC_DETUNE_PARAM,
        OSC_PITCH_PARAM,
        FILTER_CUTOFF_PARAM,
        FILTER_RESONANCE_PARAM,
        RESONATOR_TUNE_PARAM,
        RESONATOR_FEEDBACK_PARAM,
        ECHO_DENSITY_PARAM,
        ECHO_REPEATS_PARAM,
        AMBIENCE_SPACETIME_PARAM,
        AMBIENCE_DECAY_PARAM,
        MOD_SPEED_PARAM,
        MOD_LEVEL_PARAM,
        RANDOM_MODE_PARAM,
        RANDOM_AMOUNT_PARAM,

        // Faders
        INPUT_FADER,
        LOOPER_FADER,
        OSC1_FADER,
        OSC2_FADER,
        FILTER_FADER,
        RESONATOR_FADER,
        ECHO_FADER,
        AMBIENCE_FADER,

        // Buttons
        SHIFT_PARAM,
        MOD_CV_PARAM,
        RECORD_PARAM,
        RANDOM_PARAM,
        PRE_POST_PARAM,
        SS_WT_PARAM,
        CLEAR_PARAM,

        // Alt layer knobs
        LOOP_FILTER_PARAM,
        LOOP_SOS_PARAM,
        OSC_OCTAVE_PARAM,
        OSC_UNISON_PARAM,
        FILTER_MODE_PARAM,
        FILTER_POSITION_PARAM,
        RESONATOR_DISSONANCE_PARAM,
        ECHO_FILTER_PARAM,
        AMBIENCE_AUTOPAN_PARAM,
        MOD_TYPE_PARAM,

        // Mod layer knobs
        LOOP_SPEED_MOD_PARAM,
        LOOP_START_MOD_PARAM,
        LOOP_LENGTH_MOD_PARAM,
        OSC_DETUNE_MOD_PARAM,
        OSC_PITCH_MOD_PARAM,
        FILTER_CUTOFF_MOD_PARAM,
        FILTER_RESONANCE_MOD_PARAM,
        RESONATOR_TUNE_MOD_PARAM,
        RESONATOR_FEEDBACK_MOD_PARAM,
        ECHO_DENSITY_MOD_PARAM,
        ECHO_REPEATS_MOD_PARAM,
        AMBIENCE_SPACETIME_MOD_PARAM,
        AMBIENCE_DECAY_MOD_PARAM,

        // CV layer knobs
        LOOP_SPEED_CV_PARAM,
        LOOP_START_CV_PARAM,
        LOOP_LENGTH_CV_PARAM,
        OSC_DETUNE_CV_PARAM,
        OSC_PITCH_CV_PARAM,
        FILTER_CUTOFF_CV_PARAM,
        FILTER_RESONANCE_CV_PARAM,
        RESONATOR_TUNE_CV_PARAM,
        RESONATOR_FEEDBACK_CV_PARAM,
        ECHO_DENSITY_CV_PARAM,
        ECHO_REPEATS_CV_PARAM,
        AMBIENCE_SPACETIME_CV_PARAM,
        AMBIENCE_DECAY_CV_PARAM,
        PARAMS_LEN
    };

    enum InputId {
        LEFT_INPUT,
        RIGHT_INPUT,
        OSC_CV_INPUT,
        DETUNE_CV_INPUT,
        LOOP_CV_INPUT,
        FILTER_CV_INPUT,
        RESONATOR_CV_INPUT,
        ECHO_CV_INPUT,
        AMBIENCE_CV_INPUT,
        RECORD_GATE_INPUT,
        RANDOM_GATE_INPUT,
        SYNC_INPUT,
        INPUTS_LEN
    };

    enum OutputId {
        LEFT_OUTPUT,
        RIGHT_OUTPUT,
        OUTPUTS_LEN
    };

    enum LightId {
        ENUMS(INPUT_LIGHT, 3),
        ARROW_LEFT_LIGHT,
        ARROW_RIGHT_LIGHT,
        MOD_LIGHT,
        SYNC_LIGHT,
        SHIFT_BUTTON_LED,
        ENUMS(MOD_CV_BUTTON_LED, 3),
        RECORD_BUTTON_LED,
        RANDOM_BUTTON_LED,
        PRE_POST_LED,
        SS_WT_LED,
        CLEAR_BUTTON_LED,
        // Panel schematic detail indicators (res/components/oneiroi/*.svg).
        FILTER_TYPE_LP_LED,
        FILTER_TYPE_BP_LED,
        FILTER_TYPE_HP_LED,
        FILTER_TYPE_CF_LED,
        FILTER_POS_1_LED,
        FILTER_POS_2_LED,
        FILTER_POS_3_LED,
        FILTER_POS_4_LED,
        MODULATION_TYPE_RANDOM_LED,
        MODULATION_TYPE_SINE_LED,
        MODULATION_TYPE_RAMP_LED,
        MODULATION_TYPE_INVERTED_RAMP_LED,
        MODULATION_TYPE_SQUARE_LED,
        MODULATION_TYPE_SH_LED,
        MODULATION_TYPE_ENVF_LED,
        LIGHTS_LEN
    };

    enum EditMode {
        EDIT_MODE_NORMAL,
        EDIT_MODE_ALT,
        EDIT_MODE_MOD,
        EDIT_MODE_CV,
    };
    EditMode editMode = EDIT_MODE_NORMAL;

    Oneiroi_1_2_2Patch *patch = nullptr;
    static constexpr int kBlockSize = 32;
    AudioBuffer *bufferIn = nullptr;
    AudioBuffer *bufferOut = nullptr;
    int bufferIndex = 0;

    dsp::BooleanTrigger recordButtonTrigger;
    dsp::BooleanTrigger randomButtonTrigger;
    dsp::BooleanTrigger clearButtonTrigger;
    dsp::SchmittTrigger recordInTrigger;
    dsp::SchmittTrigger randomInTrigger;
    dsp::SchmittTrigger syncInTrigger;

    uint8_t randomTask = 0;
    float randomLedBrightness = 0.f;
    float arrowFlashTime = 0.f;

    struct ModTypeParamQuantity : ParamQuantity {
        std::string getDisplayValueString() override {
            switch ((int)std::round(getValue())) {
            case 0: return "Lorenz attractor";
            case 1: return "Sine";
            case 2: return "Ramp";
            case 3: return "Inverted ramp";
            case 4: return "Square";
            case 5: return "Sample & hold";
            case 6: return "Envelope follower";
            default: return "Not recognised!";
            }
        }
    };

    struct FilterModeParamQuantity : ParamQuantity {
        std::string getDisplayValueString() override {
            switch ((int)std::round(getValue())) {
            case 0: return "Lowpass";
            case 1: return "Bandpass";
            case 2: return "Highpass";
            case 3: return "Comb filter";
            default: return "Not recognised!";
            }
        }
    };

    struct FilterPositionParamQuantity : ParamQuantity {
        std::string getDisplayValueString() override {
            switch ((int)std::round(getValue())) {
            case 0: return "Pre-resonator";
            case 1: return "Post-resonator";
            case 2: return "Post-echo";
            case 3: return "Post-ambience";
            default: return "Not recognised!";
            }
        }
    };

    OneiroiVCV() {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

        configParam(LOOP_SPEED_PARAM, 0.f, 1.f, 0.28f, "Looper speed");
        configParam(LOOP_START_PARAM, 0.f, 1.f, 0.5f, "Looper start");
        configParam(LOOP_LENGTH_PARAM, 0.f, 1.f, 0.35f, "Looper length");
        configParam(OSC_DETUNE_PARAM, 0.f, 1.f, 0.5f, "Osc detune");
        configParam(OSC_PITCH_PARAM, 0.f, 1.f, 0.5f, "Osc pitch");
        configParam(FILTER_CUTOFF_PARAM, 0.f, 1.f, 1.f, "Filter cutoff");
        configParam(FILTER_RESONANCE_PARAM, 0.f, 1.f, 0.15f, "Filter resonance");
        configParam(RESONATOR_TUNE_PARAM, 0.f, 1.f, 0.5f, "Resonator tune");
        configParam(RESONATOR_FEEDBACK_PARAM, 0.f, 1.f, 0.5f, "Resonator feedback");
        configParam(ECHO_DENSITY_PARAM, 0.f, 1.f, 0.5f, "Echo density", " ", "s");
        configParam(ECHO_REPEATS_PARAM, 0.f, 1.f, 0.35f, "Echo repeats");
        configParam(AMBIENCE_SPACETIME_PARAM, 0.f, 1.f, 0.5f, "Ambience spacetime");
        configParam(AMBIENCE_DECAY_PARAM, 0.f, 1.f, 0.5f, "Ambience decay", " ", "s");
        configParam(MOD_SPEED_PARAM, 0.f, 1.f, 0.3f, "Modulation speed");
        configParam(MOD_LEVEL_PARAM, 0.f, 1.f, 0.f, "Modulation level", "%", 0.f, 100.f);
        configSwitch(RANDOM_MODE_PARAM, 0.f, 3.f, 0.f, "Randomize mode",
                     {"All", "Oscillators", "Looper", "Effects"});
        configSwitch(RANDOM_AMOUNT_PARAM, 0.f, 2.f, 1.f, "Randomize amount", {"Low", "Mid", "High"});

        configParam(INPUT_FADER, 0.f, 1.f, 1.f, "Input volume", "%", 0.f, 100.f);
        configParam(LOOPER_FADER, 0.f, 1.f, 1.f, "Looper volume", "%", 0.f, 100.f);
        configParam(OSC1_FADER, 0.f, 1.f, 0.8f, "Oscillator 1 volume", "%", 0.f, 100.f);
        configParam(OSC2_FADER, 0.f, 1.f, 0.7f, "Oscillator 2 volume", "%", 0.f, 100.f);
        configParam(FILTER_FADER, 0.f, 1.f, 1.f, "Filter VCA", "%", 0.f, 100.f);
        configParam(RESONATOR_FADER, 0.f, 1.f, 0.5f, "Resonator dry/wet", "%", 0.f, 100.f);
        configParam(ECHO_FADER, 0.f, 1.f, 0.4f, "Echo dry/wet", "%", 0.f, 100.f);
        configParam(AMBIENCE_FADER, 0.f, 1.f, 0.4f, "Ambience dry/wet", "%", 0.f, 100.f);

        auto shiftButton = configButton(SHIFT_PARAM, "Shift");
        shiftButton->description = "Shows the alt parameter layer. Hold with MOD/CV for the CV layer.";
        auto modCvButton = configButton(MOD_CV_PARAM, "Mod/CV");
        modCvButton->description = "Latch to show the modulation amount layer. With Shift, shows CV amounts.";
        auto recordButton = configButton(RECORD_PARAM, "Record");
        recordButton->description = "Starts/stops looper recording (same function as the RECORD gate input).";
        auto randomButton = configButton(RANDOM_PARAM, "Randomize");
        randomButton->description = "Triggers a randomization event (same function as the RANDOM gate input).";
        auto prePost = configSwitch(PRE_POST_PARAM, 0.f, 1.f, 0.f, "Looper resampling", {"Off", "On"});
        prePost->description = "Feeds the looper through the time-stretch/resampling engine.";
        auto ssWt = configSwitch(SS_WT_PARAM, 0.f, 1.f, 0.f, "Osc 2 style", {"Super saw", "Wavetable"});
        ssWt->description = "Selects the oscillator 2 waveform: super saw or wavetable.";
        auto clearButton = configButton(CLEAR_PARAM, "Clear looper");
        clearButton->description = "Clears the looper buffer.";

        // Alt layer
        configParam(LOOP_FILTER_PARAM, 0.f, 1.f, 0.55f, "Looper filter");
        configParam(LOOP_SOS_PARAM, 0.f, 1.f, 0.f, "Looper SOS (second-order section)");
        configParam(OSC_OCTAVE_PARAM, 0.f, 1.f, 0.375f, "Osc octave");
        configParam(OSC_UNISON_PARAM, 0.f, 1.f, 0.55f, "Osc unison");
        auto filterMode = configParam<FilterModeParamQuantity>(FILTER_MODE_PARAM, 0.f, 3.f, 0.f, "Filter mode");
        filterMode->snapEnabled = true;
        auto filterPosition = configParam<FilterPositionParamQuantity>(FILTER_POSITION_PARAM, 0.f, 3.f, 0.f, "Filter position");
        filterPosition->snapEnabled = true;
        configParam(RESONATOR_DISSONANCE_PARAM, 0.f, 1.f, 0.5f, "Resonator dissonance");
        configParam(ECHO_FILTER_PARAM, 0.f, 1.f, 0.5f, "Echo filter");
        configParam(AMBIENCE_AUTOPAN_PARAM, 0.f, 1.f, 0.5f, "Ambience autopan");
        auto modType = configParam<ModTypeParamQuantity>(MOD_TYPE_PARAM, 0.f, 6.f, 0.f, "Modulation type");
        modType->snapEnabled = true;

        // Mod layer
        configParam(LOOP_SPEED_MOD_PARAM, 0.f, 1.f, 0.f, "Looper speed mod amount", "%", 0.f, 100.f);
        configParam(LOOP_START_MOD_PARAM, 0.f, 1.f, 0.f, "Looper start mod amount", "%", 0.f, 100.f);
        configParam(LOOP_LENGTH_MOD_PARAM, 0.f, 1.f, 0.f, "Looper length mod amount", "%", 0.f, 100.f);
        configParam(OSC_DETUNE_MOD_PARAM, 0.f, 1.f, 0.f, "Osc detune mod amount", "%", 0.f, 100.f);
        configParam(OSC_PITCH_MOD_PARAM, 0.f, 1.f, 0.f, "Osc pitch mod amount", "%", 0.f, 100.f);
        configParam(FILTER_CUTOFF_MOD_PARAM, 0.f, 1.f, 0.5f, "Filter cutoff mod amount", "%", 0.f, 100.f);
        configParam(FILTER_RESONANCE_MOD_PARAM, 0.f, 1.f, 0.f, "Filter resonance mod amount", "%", 0.f, 100.f);
        configParam(RESONATOR_TUNE_MOD_PARAM, 0.f, 1.f, 0.f, "Resonator tune mod amount", "%", 0.f, 100.f);
        configParam(RESONATOR_FEEDBACK_MOD_PARAM, 0.f, 1.f, 0.f, "Resonator feedback mod amount", "%", 0.f, 100.f);
        configParam(ECHO_DENSITY_MOD_PARAM, 0.f, 1.f, 0.f, "Echo density mod amount", "%", 0.f, 100.f);
        configParam(ECHO_REPEATS_MOD_PARAM, 0.f, 1.f, 0.f, "Echo repeats mod amount", "%", 0.f, 100.f);
        configParam(AMBIENCE_SPACETIME_MOD_PARAM, 0.f, 1.f, 0.f, "Ambience spacetime mod amount", "%", 0.f, 100.f);
        configParam(AMBIENCE_DECAY_MOD_PARAM, 0.f, 1.f, 0.f, "Ambience decay mod amount", "%", 0.f, 100.f);

        // CV layer
        configParam(LOOP_SPEED_CV_PARAM, 0.f, 1.f, 1.f, "Looper speed CV amount", "%", 0.f, 100.f);
        configParam(LOOP_START_CV_PARAM, 0.f, 1.f, 1.f, "Looper start CV amount", "%", 0.f, 100.f);
        configParam(LOOP_LENGTH_CV_PARAM, 0.f, 1.f, 1.f, "Looper length CV amount", "%", 0.f, 100.f);
        configParam(OSC_DETUNE_CV_PARAM, 0.f, 1.f, 1.f, "Osc detune CV amount", "%", 0.f, 100.f);
        configParam(OSC_PITCH_CV_PARAM, 0.f, 1.f, 1.f, "Osc pitch CV amount", "%", 0.f, 100.f);
        configParam(FILTER_CUTOFF_CV_PARAM, 0.f, 1.f, 1.f, "Filter cutoff CV amount", "%", 0.f, 100.f);
        configParam(FILTER_RESONANCE_CV_PARAM, 0.f, 1.f, 0.f, "Filter resonance CV amount", "%", 0.f, 100.f);
        configParam(RESONATOR_TUNE_CV_PARAM, 0.f, 1.f, 1.f, "Resonator tune CV amount", "%", 0.f, 100.f);
        configParam(RESONATOR_FEEDBACK_CV_PARAM, 0.f, 1.f, 0.f, "Resonator feedback CV amount", "%", 0.f, 100.f);
        configParam(ECHO_DENSITY_CV_PARAM, 0.f, 1.f, 1.f, "Echo density CV amount", "%", 0.f, 100.f);
        configParam(ECHO_REPEATS_CV_PARAM, 0.f, 1.f, 0.f, "Echo repeats CV amount", "%", 0.f, 100.f);
        configParam(AMBIENCE_SPACETIME_CV_PARAM, 0.f, 1.f, 1.f, "Ambience spacetime CV amount", "%", 0.f, 100.f);
        configParam(AMBIENCE_DECAY_CV_PARAM, 0.f, 1.f, 0.f, "Ambience decay CV amount", "%", 0.f, 100.f);

        configInput(LEFT_INPUT, "Left audio in");
        configInput(RIGHT_INPUT, "Right audio in");
        auto oscCvInput = configInput(OSC_CV_INPUT, "Osc pitch CV");
        oscCvInput->description = "Pitch CV (V/oct, bipolar).";
        auto detuneCvInput = configInput(DETUNE_CV_INPUT, "Osc detune CV");
        detuneCvInput->description = "Expected CV range: -5V to +10V.";
        auto loopCvInput = configInput(LOOP_CV_INPUT, "Looper speed CV");
        loopCvInput->description = "Expected CV range: -5V to +10V.";
        auto filterCvInput = configInput(FILTER_CV_INPUT, "Filter cutoff CV");
        filterCvInput->description = "Expected CV range: -5V to +10V.";
        auto resonatorCvInput = configInput(RESONATOR_CV_INPUT, "Resonator tune CV");
        resonatorCvInput->description = "Expected CV range: -5V to +10V.";
        auto echoCvInput = configInput(ECHO_CV_INPUT, "Echo density CV");
        echoCvInput->description = "Expected CV range: -5V to +10V.";
        auto ambienceCvInput = configInput(AMBIENCE_CV_INPUT, "Ambience spacetime CV");
        ambienceCvInput->description = "Expected CV range: -5V to +10V.";
        auto recordGateInput = configInput(RECORD_GATE_INPUT, "Record gate");
        recordGateInput->description = "High gate starts/stops looper recording.";
        auto randomGateInput = configInput(RANDOM_GATE_INPUT, "Randomize gate");
        randomGateInput->description = "Rising edge triggers a randomization event.";
        auto syncInput = configInput(SYNC_INPUT, "Sync");
        syncInput->description = "Clock/sync input for time-based sections.";

        configOutput(LEFT_OUTPUT, "Left audio out");
        configOutput(RIGHT_OUTPUT, "Right audio out");

        configBypass(LEFT_INPUT, LEFT_OUTPUT);
        configBypass(RIGHT_INPUT, RIGHT_OUTPUT);

        fast_log_set_table(fast_log_table, fast_log_table_size);
        fast_pow_set_table(fast_pow_table, fast_pow_table_size);

        patch = new Oneiroi_1_2_2Patch();

        bufferIn = AudioBuffer::create(2, kBlockSize);
        bufferOut = AudioBuffer::create(2, kBlockSize);
    }

    ~OneiroiVCV() {
        delete patch;
        AudioBuffer::destroy(bufferIn);
        AudioBuffer::destroy(bufferOut);
    }

    void onAdd(const AddEvent &e) override {
        Module::onAdd(e);
        // SHIFT and MOD/CV are latching edit-mode modifiers; force them off on
        // load so patches don't unexpectedly open in ALT/MOD/CV edit mode.
        params[SHIFT_PARAM].setValue(0.f);
        params[MOD_CV_PARAM].setValue(0.f);
        editMode = EDIT_MODE_NORMAL;
    }

    void onSampleRateChange() override {
        float newSampleRate = APP->engine->getSampleRate();
        float oldSampleRate = patch->getPatchState()->sampleRate;
        // This destroys and recreates the DSP, only do it on a real change.
        if (newSampleRate != oldSampleRate) {
            patch->updateSampleRateBlockSize(APP->engine->getSampleRate(), kBlockSize);
        }
    }

    void updateCv(PatchCvs *patchCvs) {
        patchCvs->looperSpeed = clamp(inputs[LOOP_CV_INPUT].getVoltage() / 10.f, -0.5f, 1.f);
        patchCvs->looperStart = 0.f;
        patchCvs->looperLength = 0.f;
        patchCvs->oscPitch = clamp(inputs[OSC_CV_INPUT].getVoltage(), -10.f, 10.f);
        patchCvs->oscDetune = clamp(inputs[DETUNE_CV_INPUT].getVoltage() / 10.f, -0.5f, 1.f);
        patchCvs->filterCutoff = clamp(inputs[FILTER_CV_INPUT].getVoltage() / 10.f, -0.5f, 1.f);
        patchCvs->filterResonance = 0.f;
        patchCvs->resonatorTune = clamp(inputs[RESONATOR_CV_INPUT].getVoltage() / 10.f, -0.5f, 1.f);
        patchCvs->resonatorFeedback = 0.f;
        patchCvs->echoDensity = clamp(inputs[ECHO_CV_INPUT].getVoltage() / 10.f, -0.5f, 1.f);
        patchCvs->echoRepeats = 0.f;
        patchCvs->ambienceSpacetime = clamp(inputs[AMBIENCE_CV_INPUT].getVoltage() / 10.f, -0.5f, 1.f);
        patchCvs->ambienceDecay = 0.f;
    }

    void updatePatchParameters(PatchCtrls *patchCtrls, PatchCvs *patchCvs) {
        // Faders use the same quadratic gain law as the firmware (MapExpo).
        patchCtrls->inputVol = MapExpo(params[INPUT_FADER].getValue());
        patchCtrls->looperVol = MapExpo(params[LOOPER_FADER].getValue());
        patchCtrls->osc1Vol = MapExpo(params[OSC1_FADER].getValue());
        patchCtrls->osc2Vol = MapExpo(params[OSC2_FADER].getValue());
        patchCtrls->filterVol = MapExpo(params[FILTER_FADER].getValue());
        patchCtrls->resonatorVol = MapExpo(params[RESONATOR_FADER].getValue());
        patchCtrls->echoVol = MapExpo(params[ECHO_FADER].getValue());
        patchCtrls->ambienceVol = MapExpo(params[AMBIENCE_FADER].getValue());

        // Looper
        patchCtrls->looperSpeed = params[LOOP_SPEED_PARAM].getValue();
        patchCtrls->looperStart = params[LOOP_START_PARAM].getValue();
        patchCtrls->looperLength = params[LOOP_LENGTH_PARAM].getValue();
        patchCtrls->looperSos = params[LOOP_SOS_PARAM].getValue();
        patchCtrls->looperFilter = params[LOOP_FILTER_PARAM].getValue();
        patchCtrls->looperResampling = params[PRE_POST_PARAM].getValue();

        // Oscillators
        const int oscOctave = 1 + (int)std::lroundf(params[OSC_OCTAVE_PARAM].getValue() * 8.f);
        const float oscPitchCvSemi = patchCvs->oscPitch * 12.f * params[OSC_PITCH_CV_PARAM].getValue();
        patchCtrls->oscOctave = oscOctave;
        patchCtrls->oscPitch =
            M2F(12.f * (params[OSC_PITCH_PARAM].getValue() - 0.5f) + 12.f * oscOctave + oscPitchCvSemi);
        patchCtrls->oscDetune = params[OSC_DETUNE_PARAM].getValue();
        patchCtrls->oscUnison = clamp(CenterMap(params[OSC_UNISON_PARAM].getValue()), -1.f, 1.f);
        patchCtrls->oscUseWavetable = params[SS_WT_PARAM].getValue();

        // Filter
        patchCtrls->filterCutoff = params[FILTER_CUTOFF_PARAM].getValue();
        patchCtrls->filterResonance = params[FILTER_RESONANCE_PARAM].getValue();
        patchCtrls->filterMode = params[FILTER_MODE_PARAM].getValue() / 4.f;
        patchCtrls->filterPosition = params[FILTER_POSITION_PARAM].getValue() / 4.f;

        // Resonator
        patchCtrls->resonatorTune = params[RESONATOR_TUNE_PARAM].getValue();
        patchCtrls->resonatorFeedback = params[RESONATOR_FEEDBACK_PARAM].getValue();
        patchCtrls->resonatorDissonance = params[RESONATOR_DISSONANCE_PARAM].getValue();

        // Echo
        patchCtrls->echoDensity = params[ECHO_DENSITY_PARAM].getValue();
        patchCtrls->echoRepeats = params[ECHO_REPEATS_PARAM].getValue();
        patchCtrls->echoFilter = params[ECHO_FILTER_PARAM].getValue();

        // Ambience
        patchCtrls->ambienceSpacetime = params[AMBIENCE_SPACETIME_PARAM].getValue();
        patchCtrls->ambienceDecay = params[AMBIENCE_DECAY_PARAM].getValue();
        patchCtrls->ambienceAutoPan = params[AMBIENCE_AUTOPAN_PARAM].getValue();

        // Modulation
        patchCtrls->modSpeed = params[MOD_SPEED_PARAM].getValue();
        patchCtrls->modLevel = params[MOD_LEVEL_PARAM].getValue();
        patchCtrls->modType = params[MOD_TYPE_PARAM].getValue() / 6.f;

        patchCtrls->randomMode = params[RANDOM_MODE_PARAM].getValue() / 3.f;
        patchCtrls->randomAmount = params[RANDOM_AMOUNT_PARAM].getValue() / 2.f;

        // Mod amounts
        patchCtrls->looperSpeedModAmount = params[LOOP_SPEED_MOD_PARAM].getValue();
        patchCtrls->looperStartModAmount = params[LOOP_START_MOD_PARAM].getValue();
        patchCtrls->looperLengthModAmount = params[LOOP_LENGTH_MOD_PARAM].getValue();
        patchCtrls->oscDetuneModAmount = params[OSC_DETUNE_MOD_PARAM].getValue();
        patchCtrls->oscPitchModAmount = params[OSC_PITCH_MOD_PARAM].getValue();
        patchCtrls->filterCutoffModAmount = params[FILTER_CUTOFF_MOD_PARAM].getValue();
        patchCtrls->filterResonanceModAmount = params[FILTER_RESONANCE_MOD_PARAM].getValue();
        patchCtrls->resonatorTuneModAmount = params[RESONATOR_TUNE_MOD_PARAM].getValue();
        patchCtrls->resonatorFeedbackModAmount = params[RESONATOR_FEEDBACK_MOD_PARAM].getValue();
        patchCtrls->echoDensityModAmount = params[ECHO_DENSITY_MOD_PARAM].getValue();
        patchCtrls->echoRepeatsModAmount = params[ECHO_REPEATS_MOD_PARAM].getValue();
        patchCtrls->ambienceSpacetimeModAmount = params[AMBIENCE_SPACETIME_MOD_PARAM].getValue();
        patchCtrls->ambienceDecayModAmount = params[AMBIENCE_DECAY_MOD_PARAM].getValue();

        // CV amounts
        patchCtrls->looperSpeedCvAmount = params[LOOP_SPEED_CV_PARAM].getValue();
        patchCtrls->looperStartCvAmount = params[LOOP_START_CV_PARAM].getValue();
        patchCtrls->looperLengthCvAmount = params[LOOP_LENGTH_CV_PARAM].getValue();
        patchCtrls->oscDetuneCvAmount = params[OSC_DETUNE_CV_PARAM].getValue();
        patchCtrls->oscPitchCvAmount = params[OSC_PITCH_CV_PARAM].getValue();
        patchCtrls->filterCutoffCvAmount = params[FILTER_CUTOFF_CV_PARAM].getValue();
        patchCtrls->filterResonanceCvAmount = params[FILTER_RESONANCE_CV_PARAM].getValue();
        patchCtrls->resonatorTuneCvAmount = params[RESONATOR_TUNE_CV_PARAM].getValue();
        patchCtrls->resonatorFeedbackCvAmount = params[RESONATOR_FEEDBACK_CV_PARAM].getValue();
        patchCtrls->echoDensityCvAmount = params[ECHO_DENSITY_CV_PARAM].getValue();
        patchCtrls->echoRepeatsCvAmount = params[ECHO_REPEATS_CV_PARAM].getValue();
        patchCtrls->ambienceSpacetimeCvAmount = params[AMBIENCE_SPACETIME_CV_PARAM].getValue();
        patchCtrls->ambienceDecayCvAmount = params[AMBIENCE_DECAY_CV_PARAM].getValue();
    }

    void jitterParam(ParamId paramId, float amount) {
        const float value = params[paramId].getValue();
        const float jitter = (rack::random::uniform() - rack::random::uniform()) * amount;
        params[paramId].setValue(clamp(value + jitter, 0.f, 1.f));
    }

    void doRandomize() {
        const int mode = (int)std::round(params[RANDOM_MODE_PARAM].getValue());

        // RANDOM_ALL cycles through Oscillators, Looper, Effects.
        int task = 0;
        if (mode == 0) {
            task = (randomTask++) % 3;
        } else {
            task = mode - 1;
        }

        static constexpr float kLowAmount = 0.33f;
        static constexpr float kMidAmount = 0.66f;
        static constexpr float kHighAmount = 1.f;
        const int amountIdx = (int)std::round(params[RANDOM_AMOUNT_PARAM].getValue());
        const float amount = amountIdx == 0 ? kLowAmount : (amountIdx == 1 ? kMidAmount : kHighAmount);

        switch (task) {
        case 0: // Oscillators
            jitterParam(OSC_PITCH_PARAM, amount);
            jitterParam(OSC_DETUNE_PARAM, amount);
            break;
        case 1: // Looper
            jitterParam(LOOP_SPEED_PARAM, amount);
            jitterParam(LOOP_START_PARAM, amount);
            jitterParam(LOOP_LENGTH_PARAM, amount);
            break;
        default: // Effects
            jitterParam(FILTER_CUTOFF_PARAM, amount);
            jitterParam(FILTER_RESONANCE_PARAM, amount);
            jitterParam(RESONATOR_TUNE_PARAM, amount);
            jitterParam(RESONATOR_FEEDBACK_PARAM, amount);
            jitterParam(ECHO_DENSITY_PARAM, amount);
            jitterParam(ECHO_REPEATS_PARAM, amount);
            jitterParam(AMBIENCE_SPACETIME_PARAM, amount);
            jitterParam(AMBIENCE_DECAY_PARAM, amount);
            break;
        }

        randomLedBrightness = 1.f;
    }

    void onRandomize(const RandomizeEvent &e) override {
        static const ParamId mainParams[] = {
            LOOP_SPEED_PARAM, LOOP_START_PARAM, LOOP_LENGTH_PARAM, OSC_DETUNE_PARAM, OSC_PITCH_PARAM,
            FILTER_CUTOFF_PARAM, FILTER_RESONANCE_PARAM, RESONATOR_TUNE_PARAM, RESONATOR_FEEDBACK_PARAM,
            ECHO_DENSITY_PARAM, ECHO_REPEATS_PARAM, AMBIENCE_SPACETIME_PARAM, AMBIENCE_DECAY_PARAM,
            LOOP_FILTER_PARAM, LOOP_SOS_PARAM, OSC_OCTAVE_PARAM, OSC_UNISON_PARAM, RESONATOR_DISSONANCE_PARAM,
            ECHO_FILTER_PARAM, AMBIENCE_AUTOPAN_PARAM, MOD_SPEED_PARAM, MOD_LEVEL_PARAM,
        };
        for (ParamId id : mainParams) {
            params[id].setValue(rack::random::uniform());
        }
    }

    void processButtons() {
        auto recordEvent = recordButtonTrigger.processEvent(params[RECORD_PARAM].getValue());
        if (recordEvent == dsp::BooleanTrigger::Event::TRIGGERED) {
            patch->buttonChanged(RECORD_BUTTON, Patch::ON, 0);
        } else if (recordEvent == dsp::BooleanTrigger::Event::UNTRIGGERED) {
            patch->buttonChanged(RECORD_BUTTON, Patch::OFF, 0);
        }

        auto randomEvent = randomButtonTrigger.processEvent(params[RANDOM_PARAM].getValue());
        if (randomEvent == dsp::BooleanTrigger::Event::TRIGGERED) {
            patch->buttonChanged(RANDOM_BUTTON, Patch::ON, 0);
            doRandomize();
        } else if (randomEvent == dsp::BooleanTrigger::Event::UNTRIGGERED) {
            patch->buttonChanged(RANDOM_BUTTON, Patch::OFF, 0);
        }

        auto clearEvent = clearButtonTrigger.processEvent(params[CLEAR_PARAM].getValue());
        if (clearEvent == dsp::BooleanTrigger::Event::TRIGGERED) {
            patch->getPatchState()->clearLooperFlag = true;
            arrowFlashTime = 0.5f;
        }

        auto recordInEvent = recordInTrigger.process(inputs[RECORD_GATE_INPUT].getVoltage());
        if (recordInEvent == dsp::SchmittTrigger::Event::TRIGGERED) {
            patch->buttonChanged(RECORD_IN, Patch::ON, 0);
        } else if (recordInEvent == dsp::SchmittTrigger::Event::UNTRIGGERED) {
            patch->buttonChanged(RECORD_IN, Patch::OFF, 0);
        }

        auto randomInEvent = randomInTrigger.process(inputs[RANDOM_GATE_INPUT].getVoltage());
        if (randomInEvent == dsp::SchmittTrigger::Event::TRIGGERED) {
            patch->buttonChanged(RANDOM_IN, Patch::ON, 0);
            doRandomize();
        } else if (randomInEvent == dsp::SchmittTrigger::Event::UNTRIGGERED) {
            patch->buttonChanged(RANDOM_IN, Patch::OFF, 0);
        }

        auto syncEvent = syncInTrigger.process(inputs[SYNC_INPUT].getVoltage());
        if (syncEvent == dsp::SchmittTrigger::Event::TRIGGERED) {
            patch->buttonChanged(SYNC_IN, Patch::ON, 0);
        } else if (syncEvent == dsp::SchmittTrigger::Event::UNTRIGGERED) {
            patch->buttonChanged(SYNC_IN, Patch::OFF, 0);
        }
    }

    void updateLights(const ProcessArgs &args, const Ui *ui) {
        const float blockTime = args.sampleTime * kBlockSize;

        if (randomLedBrightness > 0.f) {
            randomLedBrightness = std::max(0.f, randomLedBrightness - blockTime * 4.f);
        }
        if (arrowFlashTime > 0.f) {
            arrowFlashTime = std::max(0.f, arrowFlashTime - blockTime);
        }

        const bool shiftActive = params[SHIFT_PARAM].getValue() > 0.5f;
        const bool modCvLatch = params[MOD_CV_PARAM].getValue() > 0.5f;

        // Input level: red = peak, green = level.
        lights[INPUT_LIGHT + 0].setBrightnessSmooth(ui->GetLed(LED_INPUT_PEAK)->Get(), blockTime);
        const bool peakActive = lights[INPUT_LIGHT + 0].getBrightness() > 0.5f;
        lights[INPUT_LIGHT + 1].setBrightnessSmooth(peakActive ? 0.f : ui->GetLed(LED_INPUT)->Get(), blockTime);
        lights[INPUT_LIGHT + 2].setBrightness(0.f);

        lights[ARROW_LEFT_LIGHT].setBrightness(
            std::max(ui->GetLed(LED_ARROW_LEFT)->Get(), arrowFlashTime > 0.f ? 1.f : 0.f));
        lights[ARROW_RIGHT_LIGHT].setBrightness(
            std::max(ui->GetLed(LED_ARROW_RIGHT)->Get(), arrowFlashTime > 0.f ? 1.f : 0.f));
        lights[MOD_LIGHT].setBrightness(ui->GetLed(LED_MOD)->Get());
        lights[SYNC_LIGHT].setBrightness(ui->GetLed(LED_SYNC)->Get());

        lights[SHIFT_BUTTON_LED].setBrightness(shiftActive);
        lights[MOD_CV_BUTTON_LED + 0].setBrightness(modCvLatch && !shiftActive);
        lights[MOD_CV_BUTTON_LED + 1].setBrightness(modCvLatch && shiftActive);
        lights[MOD_CV_BUTTON_LED + 2].setBrightness(0.f);

        lights[RECORD_BUTTON_LED].setBrightnessSmooth(ui->GetLed(LED_RECORD)->Get(), blockTime);
        lights[RANDOM_BUTTON_LED].setBrightness(std::max(ui->GetLed(LED_RANDOM)->Get(), randomLedBrightness));
        lights[PRE_POST_LED].setBrightness(params[PRE_POST_PARAM].getValue());
        lights[SS_WT_LED].setBrightness(params[SS_WT_PARAM].getValue());
        lights[CLEAR_BUTTON_LED].setBrightness(arrowFlashTime > 0.f ? 1.f : 0.f);

        // Panel schematic detail indicators: only the active option lights up.
        const int filterMode = (int)std::round(params[FILTER_MODE_PARAM].getValue());
        lights[FILTER_TYPE_LP_LED].setBrightness(filterMode == 0);
        lights[FILTER_TYPE_BP_LED].setBrightness(filterMode == 1);
        lights[FILTER_TYPE_HP_LED].setBrightness(filterMode == 2);
        lights[FILTER_TYPE_CF_LED].setBrightness(filterMode == 3);

        const int filterPosition = (int)std::round(params[FILTER_POSITION_PARAM].getValue());
        lights[FILTER_POS_1_LED].setBrightness(filterPosition == 0);
        lights[FILTER_POS_2_LED].setBrightness(filterPosition == 1);
        lights[FILTER_POS_3_LED].setBrightness(filterPosition == 2);
        lights[FILTER_POS_4_LED].setBrightness(filterPosition == 3);

        const int modType = (int)std::round(params[MOD_TYPE_PARAM].getValue());
        lights[MODULATION_TYPE_RANDOM_LED].setBrightness(modType == 0);
        lights[MODULATION_TYPE_SINE_LED].setBrightness(modType == 1);
        lights[MODULATION_TYPE_RAMP_LED].setBrightness(modType == 2);
        lights[MODULATION_TYPE_INVERTED_RAMP_LED].setBrightness(modType == 3);
        lights[MODULATION_TYPE_SQUARE_LED].setBrightness(modType == 4);
        lights[MODULATION_TYPE_SH_LED].setBrightness(modType == 5);
        lights[MODULATION_TYPE_ENVF_LED].setBrightness(modType == 6);
    }

    void process(const ProcessArgs &args) override {
        if (!patch || !bufferIn || !bufferOut) {
            return;
        }

        PatchCtrls *patchCtrls = patch->getPatchCtrls();
        PatchCvs *patchCvs = patch->getPatchCvs();
        PatchState *patchState = patch->getPatchState();
        Ui *ui = patch->getUi();

        if (!patchCtrls || !patchCvs || !patchState || !ui) {
            return;
        }

        processButtons();

        const bool shiftActive = params[SHIFT_PARAM].getValue() > 0.5f;
        const bool modCvLatch = params[MOD_CV_PARAM].getValue() > 0.5f;
        if (shiftActive && modCvLatch) {
            editMode = EDIT_MODE_CV;
        } else if (shiftActive) {
            editMode = EDIT_MODE_ALT;
        } else if (modCvLatch) {
            editMode = EDIT_MODE_MOD;
        } else {
            editMode = EDIT_MODE_NORMAL;
        }

        if (bufferIndex == kBlockSize) {
            updateCv(patchCvs);
            updatePatchParameters(patchCtrls, patchCvs);

            bufferOut->copyFrom(*bufferIn);
            patch->processAudio(*bufferOut);
            bufferIndex = 0;

            updateLights(args, ui);
        }

        bufferIn->getSamples(LEFT_CHANNEL)[bufferIndex] = inputs[LEFT_INPUT].getVoltageSum() / 5.f;
        if (inputs[RIGHT_INPUT].isConnected()) {
            bufferIn->getSamples(RIGHT_CHANNEL)[bufferIndex] = inputs[RIGHT_INPUT].getVoltageSum() / 5.f;
        } else {
            bufferIn->getSamples(RIGHT_CHANNEL)[bufferIndex] = bufferIn->getSamples(LEFT_CHANNEL)[bufferIndex];
        }

        if (outputs[RIGHT_OUTPUT].isConnected()) {
            outputs[LEFT_OUTPUT].setVoltage(5.f * bufferOut->getSamples(LEFT_CHANNEL)[bufferIndex]);
            outputs[RIGHT_OUTPUT].setVoltage(5.f * bufferOut->getSamples(RIGHT_CHANNEL)[bufferIndex]);
        } else {
            float out =
                2.5f * (bufferOut->getSamples(LEFT_CHANNEL)[bufferIndex] + bufferOut->getSamples(RIGHT_CHANNEL)[bufferIndex]);
            outputs[LEFT_OUTPUT].setVoltage(out);
        }

        bufferIndex++;
    }
};

// adapted from VCV Free
struct BefacoButtonOneiroi : app::SvgSwitch {
    BefacoButtonOneiroi() {
        momentary = true;
        addFrame(Svg::load(asset::plugin(pluginInstance, "res/components/BefacoButton.svg")));
    }
};
struct BefacoButtonOneiroiToggle : app::SvgSwitch {
    BefacoButtonOneiroiToggle() {
        momentary = false;
        addFrame(Svg::load(asset::plugin(pluginInstance, "res/components/BefacoButton.svg")));
    }
};
template <typename TBase> struct VCVBezelLightBig : TBase {
    VCVBezelLightBig() {
        this->borderColor = color::WHITE_TRANSPARENT;
        this->bgColor = color::WHITE_TRANSPARENT;
        this->box.size = mm2px(math::Vec(9, 9));
    }
};
using BefacoRedLightButton = LightButton<BefacoButtonOneiroi, VCVBezelLightBig<TRedLight<app::ModuleLightWidget>>>;
using BefacoLightButton = LightButton<BefacoButtonOneiroi, VCVBezelLightBig<TRedGreenBlueLight<app::ModuleLightWidget>>>;
using BefacoRedLightToggleButton =
    LightButton<BefacoButtonOneiroiToggle, VCVBezelLightBig<TRedLight<app::ModuleLightWidget>>>;
using BefacoRgbToggleButton =
    LightButton<BefacoButtonOneiroiToggle, VCVBezelLightBig<TRedGreenBlueLight<app::ModuleLightWidget>>>;

// Adapted from Iroi.cpp: light widgets that re-ink a schematic SVG with the
// light color. Inactive glyphs (brightness 0) render invisible, so only the
// current filter mode / position / modulation shape lights up on the panel.
template <bool filled> struct OneiroiSchematicLedT : TSvgLight<RedLight> {
    void draw(const DrawArgs &args) override {}
    void drawLayer(const DrawArgs &args, int layer) override {
        if (layer == 1) {
            if (!sw->svg) {
                return;
            }
            if (module && !module->isBypassed()) {
                for (auto s = sw->svg->handle->shapes; s; s = s->next) {
                    if (filled) {
                        s->fill.color = ((int)(color.a * 255) << 24) + (((int)(color.b * 255)) << 16) +
                                        (((int)(color.g * 255)) << 8) + (int)(color.r * 255);
                        s->fill.type = NSVG_PAINT_COLOR;
                    } else {
                        s->stroke.color = ((int)(color.a * 255) << 24) + (((int)(color.b * 255)) << 16) +
                                          (((int)(color.g * 255)) << 8) + (int)(color.r * 255);
                        s->stroke.type = NSVG_PAINT_COLOR;
                    }
                }
                nvgGlobalCompositeBlendFunc(args.vg, NVG_ONE_MINUS_DST_COLOR, NVG_ONE);
                svgDraw(args.vg, sw->svg->handle);
                drawHalo(args);
            }
        }
        Widget::drawLayer(args, layer);
    }
};
typedef OneiroiSchematicLedT<true> OneiroiSchematicShapeLed;
typedef OneiroiSchematicLedT<false> OneiroiSchematicPathLed;

struct OneiroiLedFilterTypeLowpass : OneiroiSchematicShapeLed {
    OneiroiLedFilterTypeLowpass() {
        this->setSvg(Svg::load(asset::plugin(pluginInstance, "res/components/oneiroi/filter_type_lp.svg")));
    }
};
struct OneiroiLedFilterTypeBandpass : OneiroiSchematicShapeLed {
    OneiroiLedFilterTypeBandpass() {
        this->setSvg(Svg::load(asset::plugin(pluginInstance, "res/components/oneiroi/filter_type_bp.svg")));
    }
};
struct OneiroiLedFilterTypeHighpass : OneiroiSchematicShapeLed {
    OneiroiLedFilterTypeHighpass() {
        this->setSvg(Svg::load(asset::plugin(pluginInstance, "res/components/oneiroi/filter_type_hp.svg")));
    }
};
struct OneiroiLedFilterTypeCombFilter : OneiroiSchematicShapeLed {
    OneiroiLedFilterTypeCombFilter() {
        this->setSvg(Svg::load(asset::plugin(pluginInstance, "res/components/oneiroi/filter_type_cf.svg")));
    }
};

struct OneiroiLedFilterPosition1 : OneiroiSchematicShapeLed {
    OneiroiLedFilterPosition1() {
        this->setSvg(Svg::load(asset::plugin(pluginInstance, "res/components/oneiroi/filter_pos_1.svg")));
    }
};
struct OneiroiLedFilterPosition2 : OneiroiSchematicShapeLed {
    OneiroiLedFilterPosition2() {
        this->setSvg(Svg::load(asset::plugin(pluginInstance, "res/components/oneiroi/filter_pos_2.svg")));
    }
};
struct OneiroiLedFilterPosition3 : OneiroiSchematicShapeLed {
    OneiroiLedFilterPosition3() {
        this->setSvg(Svg::load(asset::plugin(pluginInstance, "res/components/oneiroi/filter_pos_3.svg")));
    }
};
struct OneiroiLedFilterPosition4 : OneiroiSchematicShapeLed {
    OneiroiLedFilterPosition4() {
        this->setSvg(Svg::load(asset::plugin(pluginInstance, "res/components/oneiroi/filter_pos_4.svg")));
    }
};

struct OneiroiLedModRandom : OneiroiSchematicPathLed {
    OneiroiLedModRandom() {
        this->setSvg(Svg::load(asset::plugin(pluginInstance, "res/components/oneiroi/modulation_type_0_random.svg")));
    }
};
struct OneiroiLedModSine : OneiroiSchematicPathLed {
    OneiroiLedModSine() {
        this->setSvg(Svg::load(asset::plugin(pluginInstance, "res/components/oneiroi/modulation_type_1_sine.svg")));
    }
};
struct OneiroiLedModRamp : OneiroiSchematicPathLed {
    OneiroiLedModRamp() {
        this->setSvg(Svg::load(asset::plugin(pluginInstance, "res/components/oneiroi/modulation_type_2_ramp.svg")));
    }
};
struct OneiroiLedModInvertedRamp : OneiroiSchematicPathLed {
    OneiroiLedModInvertedRamp() {
        this->setSvg(Svg::load(asset::plugin(pluginInstance, "res/components/oneiroi/modulation_type_3_inverted_ramp.svg")));
    }
};
struct OneiroiLedModSquare : OneiroiSchematicPathLed {
    OneiroiLedModSquare() {
        this->setSvg(Svg::load(asset::plugin(pluginInstance, "res/components/oneiroi/modulation_type_4_square.svg")));
    }
};
struct OneiroiLedModSH : OneiroiSchematicPathLed {
    OneiroiLedModSH() {
        this->setSvg(Svg::load(asset::plugin(pluginInstance, "res/components/oneiroi/modulation_type_5_sh.svg")));
    }
};
struct OneiroiLedModEnvFollower : OneiroiSchematicShapeLed {
    OneiroiLedModEnvFollower() {
        this->setSvg(Svg::load(asset::plugin(pluginInstance, "res/components/oneiroi/modulation_type_6_envf.svg")));
    }
};

struct OneiroiWidget : ModuleWidget {
    ParamWidget *altWidgets[10] = {};
    ParamWidget *modWidgets[13] = {};
    ParamWidget *cvWidgets[13] = {};

    OneiroiWidget(OneiroiVCV *module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/panels/Oneiroi.svg")));

        addChild(createWidget<Knurlie>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<Knurlie>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
        addChild(createWidget<Knurlie>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        addChild(createWidget<Knurlie>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        // Main knobs. Left band: Oneiroi sources (rows A/B at 38.4/49.2,
        // row C at 70.8 + tiny black RAND column). Right band: Iroi FX block
        // translated +61.1mm (rows A 38.4 / B 49.2 / C 70.8 / D 78.5).
        addParam(createParamCentered<Davies1900hDarkGreyKnob>(mm2px(Vec(29.009, 38.369)), module, OneiroiVCV::LOOP_SPEED_PARAM));
        addParam(createParamCentered<Davies1900hDarkGreyKnob>(mm2px(Vec(45.648, 38.369)), module, OneiroiVCV::OSC_DETUNE_PARAM));
        addParam(createParamCentered<BefacoTinyKnobLightGrey>(mm2px(Vec(29.009, 49.153)), module, OneiroiVCV::LOOP_START_PARAM));
        addParam(createParamCentered<BefacoTinyKnobLightGrey>(mm2px(Vec(45.648, 49.153)), module, OneiroiVCV::OSC_PITCH_PARAM));
        addParam(createParamCentered<BefacoTinyKnobLightGrey>(mm2px(Vec(29.009, 70.793)), module, OneiroiVCV::LOOP_LENGTH_PARAM));
        addParam(createParamCentered<BefacoTinyKnobBlack>(mm2px(Vec(12.377, 49.17)), module, OneiroiVCV::RANDOM_MODE_PARAM));
        addParam(createParamCentered<BefacoTinyKnobBlack>(mm2px(Vec(12.377, 70.804)), module, OneiroiVCV::RANDOM_AMOUNT_PARAM));

        // Right band: Iroi FX block shifted +61.1mm.
        addParam(createParamCentered<BefacoTinyKnobBlack>(mm2px(Vec(73.477, 49.17)), module, OneiroiVCV::MOD_LEVEL_PARAM));
        addParam(createParamCentered<Davies1900hDarkGreyKnob>(mm2px(Vec(90.109, 38.369)), module, OneiroiVCV::FILTER_CUTOFF_PARAM));
        addParam(createParamCentered<BefacoTinyKnobLightGrey>(mm2px(Vec(106.748, 49.153)), module, OneiroiVCV::FILTER_RESONANCE_PARAM));
        addParam(createParamCentered<Davies1900hDarkGreyKnob>(mm2px(Vec(123.826, 38.359)), module, OneiroiVCV::RESONATOR_TUNE_PARAM));
        addParam(createParamCentered<BefacoTinyKnobLightGrey>(mm2px(Vec(140.901, 49.153)), module,
                                                              OneiroiVCV::RESONATOR_FEEDBACK_PARAM));
        addParam(createParamCentered<BefacoTinyKnobBlack>(mm2px(Vec(73.477, 70.804)), module, OneiroiVCV::MOD_SPEED_PARAM));
        addParam(createParamCentered<Davies1900hDarkGreyKnob>(mm2px(Vec(106.75, 70.793)), module, OneiroiVCV::ECHO_DENSITY_PARAM));
        addParam(createParamCentered<Davies1900hDarkGreyKnob>(mm2px(Vec(140.904, 70.765)), module,
                                                              OneiroiVCV::AMBIENCE_SPACETIME_PARAM));
        addParam(createParamCentered<BefacoTinyKnobLightGrey>(mm2px(Vec(90.108, 78.512)), module, OneiroiVCV::ECHO_REPEATS_PARAM));
        addParam(createParamCentered<BefacoTinyKnobLightGrey>(mm2px(Vec(123.825, 78.414)), module, OneiroiVCV::AMBIENCE_DECAY_PARAM));

        // Faders: BefacoSlidePotSmall (box 2.273 x 26.011 mm) at top-left y=87.842.
        // Left 4 = source mixer at 14mm pitch; right 4 = Iroi faders +61.1mm.
        addParam(createParam<BefacoSlidePotSmall>(mm2px(Vec(8, 87.842)), module, OneiroiVCV::INPUT_FADER));
        addParam(createParam<BefacoSlidePotSmall>(mm2px(Vec(22, 87.842)), module, OneiroiVCV::LOOPER_FADER));
        addParam(createParam<BefacoSlidePotSmall>(mm2px(Vec(36, 87.842)), module, OneiroiVCV::OSC1_FADER));
        addParam(createParam<BefacoSlidePotSmall>(mm2px(Vec(50, 87.842)), module, OneiroiVCV::OSC2_FADER));
        addParam(createParam<BefacoSlidePotSmall>(mm2px(Vec(71.353, 87.842)), module, OneiroiVCV::FILTER_FADER));
        addParam(createParam<BefacoSlidePotSmall>(mm2px(Vec(116.861, 87.842)), module, OneiroiVCV::RESONATOR_FADER));
        addParam(createParam<BefacoSlidePotSmall>(mm2px(Vec(130.289, 87.842)), module, OneiroiVCV::ECHO_FADER));
        addParam(createParam<BefacoSlidePotSmall>(mm2px(Vec(143.716, 87.842)), module, OneiroiVCV::AMBIENCE_FADER));

        // Buttons: right band = Iroi buttons +61.1mm (SHIFT/RANDOM at column 90.109,
        // CLEAR at 106.75). Left band = 2x2 utility grid at columns 16.1 / 44.1,
        // interleaved with the source faders (no hitbox overlap).
        addParam(createLightParamCentered<BefacoRedLightToggleButton>(mm2px(Vec(90.109, 94.492)), module, OneiroiVCV::SHIFT_PARAM,
                                                                      OneiroiVCV::SHIFT_BUTTON_LED));
        addParam(createLightParamCentered<BefacoRgbToggleButton>(mm2px(Vec(16.1, 94.492)), module, OneiroiVCV::MOD_CV_PARAM,
                                                                     OneiroiVCV::MOD_CV_BUTTON_LED));
        addParam(createLightParamCentered<BefacoRedLightButton>(mm2px(Vec(44.1, 94.492)), module, OneiroiVCV::RECORD_PARAM,
                                                                OneiroiVCV::RECORD_BUTTON_LED));
        addParam(createLightParamCentered<BefacoRedLightButton>(mm2px(Vec(90.109, 110.464)), module, OneiroiVCV::RANDOM_PARAM,
                                                                OneiroiVCV::RANDOM_BUTTON_LED));
        addParam(createLightParamCentered<BefacoRedLightToggleButton>(mm2px(Vec(16.1, 110.464)), module, OneiroiVCV::PRE_POST_PARAM,
                                                                      OneiroiVCV::PRE_POST_LED));
        addParam(createLightParamCentered<BefacoRedLightToggleButton>(mm2px(Vec(44.1, 110.464)), module, OneiroiVCV::SS_WT_PARAM,
                                                                      OneiroiVCV::SS_WT_LED));
        addParam(createLightParamCentered<BefacoRedLightButton>(mm2px(Vec(106.75, 110.464)), module, OneiroiVCV::CLEAR_PARAM,
                                                                OneiroiVCV::CLEAR_BUTTON_LED));

        // Alt layer knobs (hidden by default), red color scheme.
        auto addAltKnob = [&](ParamId paramId, float x, float y, bool large) {
            ParamWidget *w = large ? createParamCentered<Davies1900hRedKnob>(mm2px(Vec(x, y)), module, paramId)
                                   : createParamCentered<BefacoTinyKnobRed>(mm2px(Vec(x, y)), module, paramId);
            w->hide();
            addParam(w);
            return w;
        };
        altWidgets[0] = addAltKnob(OneiroiVCV::LOOP_FILTER_PARAM, 29.009, 38.369, true);
        altWidgets[1] = addAltKnob(OneiroiVCV::LOOP_SOS_PARAM, 29.009, 49.153, false);
        altWidgets[2] = addAltKnob(OneiroiVCV::OSC_OCTAVE_PARAM, 45.648, 38.369, true);
        altWidgets[3] = addAltKnob(OneiroiVCV::OSC_UNISON_PARAM, 45.648, 49.153, false);
        altWidgets[4] = addAltKnob(OneiroiVCV::FILTER_MODE_PARAM, 90.109, 38.369, true);
        altWidgets[5] = addAltKnob(OneiroiVCV::FILTER_POSITION_PARAM, 106.748, 49.153, false);
        altWidgets[6] = addAltKnob(OneiroiVCV::RESONATOR_DISSONANCE_PARAM, 123.826, 38.359, true);
        altWidgets[7] = addAltKnob(OneiroiVCV::ECHO_FILTER_PARAM, 106.75, 70.793, true);
        altWidgets[8] = addAltKnob(OneiroiVCV::AMBIENCE_AUTOPAN_PARAM, 140.904, 70.765, true);
        altWidgets[9] = addAltKnob(OneiroiVCV::MOD_TYPE_PARAM, 73.477, 70.804, false);

        // Mod layer knobs (hidden by default), green color scheme.
        auto addModKnob = [&](ParamId paramId, float x, float y, bool large) {
            ParamWidget *w = large ? createParamCentered<Davies1900hGreenKnob>(mm2px(Vec(x, y)), module, paramId)
                                   : createParamCentered<BefacoTinyKnobGreen>(mm2px(Vec(x, y)), module, paramId);
            w->hide();
            addParam(w);
            return w;
        };
        modWidgets[0] = addModKnob(OneiroiVCV::LOOP_SPEED_MOD_PARAM, 29.009, 38.369, true);
        modWidgets[1] = addModKnob(OneiroiVCV::LOOP_START_MOD_PARAM, 29.009, 49.153, false);
        modWidgets[2] = addModKnob(OneiroiVCV::LOOP_LENGTH_MOD_PARAM, 29.009, 70.793, false);
        modWidgets[3] = addModKnob(OneiroiVCV::OSC_DETUNE_MOD_PARAM, 45.648, 38.369, true);
        modWidgets[4] = addModKnob(OneiroiVCV::OSC_PITCH_MOD_PARAM, 45.648, 49.153, false);
        modWidgets[5] = addModKnob(OneiroiVCV::FILTER_CUTOFF_MOD_PARAM, 90.109, 38.369, true);
        modWidgets[6] = addModKnob(OneiroiVCV::FILTER_RESONANCE_MOD_PARAM, 106.748, 49.153, false);
        modWidgets[7] = addModKnob(OneiroiVCV::RESONATOR_TUNE_MOD_PARAM, 123.826, 38.359, true);
        modWidgets[8] = addModKnob(OneiroiVCV::RESONATOR_FEEDBACK_MOD_PARAM, 140.901, 49.153, false);
        modWidgets[9] = addModKnob(OneiroiVCV::ECHO_DENSITY_MOD_PARAM, 106.75, 70.793, true);
        modWidgets[10] = addModKnob(OneiroiVCV::ECHO_REPEATS_MOD_PARAM, 90.108, 78.512, false);
        modWidgets[11] = addModKnob(OneiroiVCV::AMBIENCE_SPACETIME_MOD_PARAM, 140.904, 70.765, true);
        modWidgets[12] = addModKnob(OneiroiVCV::AMBIENCE_DECAY_MOD_PARAM, 123.825, 78.414, false);

        // CV layer knobs (hidden by default), blue color scheme.
        auto addCvKnob = [&](ParamId paramId, float x, float y, bool large) {
            ParamWidget *w = large ? createParamCentered<Davies1900hBlueKnob>(mm2px(Vec(x, y)), module, paramId)
                                   : createParamCentered<BefacoTinyKnobBlue>(mm2px(Vec(x, y)), module, paramId);
            w->hide();
            addParam(w);
            return w;
        };
        cvWidgets[0] = addCvKnob(OneiroiVCV::LOOP_SPEED_CV_PARAM, 29.009, 38.369, true);
        cvWidgets[1] = addCvKnob(OneiroiVCV::LOOP_START_CV_PARAM, 29.009, 49.153, false);
        cvWidgets[2] = addCvKnob(OneiroiVCV::LOOP_LENGTH_CV_PARAM, 29.009, 70.793, false);
        cvWidgets[3] = addCvKnob(OneiroiVCV::OSC_DETUNE_CV_PARAM, 45.648, 38.369, true);
        cvWidgets[4] = addCvKnob(OneiroiVCV::OSC_PITCH_CV_PARAM, 45.648, 49.153, false);
        cvWidgets[5] = addCvKnob(OneiroiVCV::FILTER_CUTOFF_CV_PARAM, 90.109, 38.369, true);
        cvWidgets[6] = addCvKnob(OneiroiVCV::FILTER_RESONANCE_CV_PARAM, 106.748, 49.153, false);
        cvWidgets[7] = addCvKnob(OneiroiVCV::RESONATOR_TUNE_CV_PARAM, 123.826, 38.359, true);
        cvWidgets[8] = addCvKnob(OneiroiVCV::RESONATOR_FEEDBACK_CV_PARAM, 140.901, 49.153, false);
        cvWidgets[9] = addCvKnob(OneiroiVCV::ECHO_DENSITY_CV_PARAM, 106.75, 70.793, true);
        cvWidgets[10] = addCvKnob(OneiroiVCV::ECHO_REPEATS_CV_PARAM, 90.108, 78.512, false);
        cvWidgets[11] = addCvKnob(OneiroiVCV::AMBIENCE_SPACETIME_CV_PARAM, 140.904, 70.765, true);
        cvWidgets[12] = addCvKnob(OneiroiVCV::AMBIENCE_DECAY_CV_PARAM, 123.825, 78.414, false);

        // Jacks at y=15.005mm (Iroi jack row). Left 5 are Oneiroi sources at 12mm
        // pitch; right 9 are the Iroi jack block translated +61.1mm.
        constexpr float kJackY = 15.005f;
        addInput(createInputCentered<BefacoInputPort>(mm2px(Vec(8, kJackY)), module, OneiroiVCV::OSC_CV_INPUT));
        addInput(createInputCentered<BefacoInputPort>(mm2px(Vec(20, kJackY)), module, OneiroiVCV::DETUNE_CV_INPUT));
        addInput(createInputCentered<BefacoInputPort>(mm2px(Vec(32, kJackY)), module, OneiroiVCV::LOOP_CV_INPUT));
        addInput(createInputCentered<BefacoInputPort>(mm2px(Vec(44, kJackY)), module, OneiroiVCV::RECORD_GATE_INPUT));
        addInput(createInputCentered<BefacoInputPort>(mm2px(Vec(56, kJackY)), module, OneiroiVCV::RANDOM_GATE_INPUT));
        addInput(createInputCentered<BefacoInputPort>(mm2px(Vec(66.108, kJackY)), module, OneiroiVCV::LEFT_INPUT));
        addInput(createInputCentered<BefacoInputPort>(mm2px(Vec(76.269, kJackY)), module, OneiroiVCV::RIGHT_INPUT));
        addInput(createInputCentered<BefacoInputPort>(mm2px(Vec(86.431, kJackY)), module, OneiroiVCV::FILTER_CV_INPUT));
        addInput(createInputCentered<BefacoInputPort>(mm2px(Vec(96.592, kJackY)), module, OneiroiVCV::RESONATOR_CV_INPUT));
        addInput(createInputCentered<BefacoInputPort>(mm2px(Vec(106.753, kJackY)), module, OneiroiVCV::ECHO_CV_INPUT));
        addInput(createInputCentered<BefacoInputPort>(mm2px(Vec(116.915, kJackY)), module, OneiroiVCV::AMBIENCE_CV_INPUT));
        addInput(createInputCentered<BefacoInputPort>(mm2px(Vec(127.076, kJackY)), module, OneiroiVCV::SYNC_INPUT));
        addOutput(createOutputCentered<BefacoOutputPort>(mm2px(Vec(137.237, kJackY)), module, OneiroiVCV::LEFT_OUTPUT));
        addOutput(createOutputCentered<BefacoOutputPort>(mm2px(Vec(147.399, kJackY)), module, OneiroiVCV::RIGHT_OUTPUT));

        // Lights
        addChild(createLightCentered<MediumLight<RedGreenBlueLight>>(mm2px(Vec(13.5, 38.5)), module, OneiroiVCV::INPUT_LIGHT));
        addChild(createLightCentered<SmallLight<RedLight>>(mm2px(Vec(23.5, 60)), module, OneiroiVCV::ARROW_LEFT_LIGHT));
        addChild(createLightCentered<SmallLight<RedLight>>(mm2px(Vec(34.5, 60)), module, OneiroiVCV::ARROW_RIGHT_LIGHT));
        addChild(createLightCentered<SmallLight<RedLight>>(mm2px(Vec(73.469, 59.976)), module, OneiroiVCV::MOD_LIGHT));
        addChild(createLightCentered<SmallLight<RedLight>>(mm2px(Vec(127.5, 32)), module, OneiroiVCV::SYNC_LIGHT));

        // Filter type glyphs around the FILTER knob, mirroring Iroi translated +61.1mm
        // (IroiWidget coordinates, including its -0.4 x nudge).
        {
            addChild(createLight<OneiroiLedFilterTypeLowpass>(mm2px(Vec(81.888878, 44.5849)), module,
                                                              OneiroiVCV::FILTER_TYPE_LP_LED));
            addChild(createLight<OneiroiLedFilterTypeBandpass>(mm2px(Vec(81.888878, 30.0338)), module,
                                                               OneiroiVCV::FILTER_TYPE_BP_LED));
            addChild(createLight<OneiroiLedFilterTypeHighpass>(mm2px(Vec(95.620087, 30.0338)), module,
                                                               OneiroiVCV::FILTER_TYPE_HP_LED));
            addChild(createLight<OneiroiLedFilterTypeCombFilter>(mm2px(Vec(95.620344, 44.5849)), module,
                                                                 OneiroiVCV::FILTER_TYPE_CF_LED));
        }
        {
            // Filter position glyphs (pre/post resonator/echo/ambience).
            addChild(createLight<OneiroiLedFilterPosition1>(mm2px(Vec(99.172104, 52.3725)), module,
                                                            OneiroiVCV::FILTER_POS_1_LED));
            addChild(createLight<OneiroiLedFilterPosition2>(mm2px(Vec(100.559615, 42.2005)), module,
                                                            OneiroiVCV::FILTER_POS_2_LED));
            addChild(createLight<OneiroiLedFilterPosition3>(mm2px(Vec(111.05912, 42.2005)), module,
                                                            OneiroiVCV::FILTER_POS_3_LED));
            addChild(createLight<OneiroiLedFilterPosition4>(mm2px(Vec(112.401269, 52.3725)), module,
                                                            OneiroiVCV::FILTER_POS_4_LED));
        }
        {
            // Modulation shape glyphs around the MOD knob, mirroring Iroi +61.1mm.
            addChild(
                createLight<OneiroiLedModRandom>(mm2px(Vec(66.6172, 73.7293)), module, OneiroiVCV::MODULATION_TYPE_RANDOM_LED));
            addChild(createLight<OneiroiLedModSine>(mm2px(Vec(65.4874, 69.0126)), module,
                                                    OneiroiVCV::MODULATION_TYPE_SINE_LED));
            addChild(createLight<OneiroiLedModRamp>(mm2px(Vec(67.5419, 64.1951)), module,
                                                    OneiroiVCV::MODULATION_TYPE_RAMP_LED));
            addChild(createLight<OneiroiLedModInvertedRamp>(mm2px(Vec(72.1332, 62.6996)), module,
                                                            OneiroiVCV::MODULATION_TYPE_INVERTED_RAMP_LED));
            addChild(createLight<OneiroiLedModSquare>(mm2px(Vec(78.0580, 64.4493)), module,
                                                      OneiroiVCV::MODULATION_TYPE_SQUARE_LED));
            addChild(createLight<OneiroiLedModSH>(mm2px(Vec(80.1408, 69.0589)), module,
                                                  OneiroiVCV::MODULATION_TYPE_SH_LED));
            addChild(createLight<OneiroiLedModEnvFollower>(mm2px(Vec(77.8897, 73.7045)), module,
                                                           OneiroiVCV::MODULATION_TYPE_ENVF_LED));
        }
    }

    void step() override {
        OneiroiVCV *oneiroi = dynamic_cast<OneiroiVCV *>(module);
        auto editMode = oneiroi ? oneiroi->editMode : OneiroiVCV::EDIT_MODE_NORMAL;

        const bool showAlt = editMode == OneiroiVCV::EDIT_MODE_ALT;
        const bool showMod = editMode == OneiroiVCV::EDIT_MODE_MOD;
        const bool showCv = editMode == OneiroiVCV::EDIT_MODE_CV;

        for (ParamWidget *w : altWidgets) {
            w->visible = showAlt;
        }
        for (ParamWidget *w : modWidgets) {
            w->visible = showMod;
        }
        for (ParamWidget *w : cvWidgets) {
            w->visible = showCv;
        }

        ModuleWidget::step();
    }
};

Model *modelOneiroi = createModel<OneiroiVCV, OneiroiWidget>("Oneiroi");