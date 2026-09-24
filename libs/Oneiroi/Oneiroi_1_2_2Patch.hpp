#ifndef __Oneiroi_1_2_2Patch_hpp__
#define __Oneiroi_1_2_2Patch_hpp__

#include "Commons.h"
#include "Ui.h"
#include "Clock.h"

namespace befacomod {
class Oneiroi_1_2_2Patch : public Patch {
private:
    Ui* ui_;
    Oneiroi* oneiroi_;
    Clock* clock_;

    PatchCtrls patchCtrls;
    PatchCvs patchCvs;
    PatchState patchState;

public:
    Oneiroi_1_2_2Patch()
    {
        patchState.sampleRate = getSampleRate();
        patchState.blockRate = getBlockRate();
        patchState.blockSize = getBlockSize();
        ui_ = Ui::create(&patchCtrls, &patchCvs, &patchState);
        oneiroi_ = Oneiroi::create(&patchCtrls, &patchCvs, &patchState);
        clock_ = Clock::create(&patchCtrls, &patchState);
    }
    ~Oneiroi_1_2_2Patch()
    {
        Oneiroi::destroy(oneiroi_);
        Ui::destroy(ui_);
        Clock::destroy(clock_);
    }

    void buttonChanged(PatchButtonId bid, uint16_t value, uint16_t samples) override
    {
        ui_->ProcessButton(bid, value, samples);
    }

    void processMidi(MidiMessage msg) override
    {
        ui_->ProcessMidi(msg);
    }

    void processAudio(AudioBuffer& buffer) override
    {
        clock_->Process();
        ui_->Poll();
        oneiroi_->Process(buffer);
    }

    void updateSampleRateBlockSize(float newSampleRate, int blockSize)
    {
        patchState.sampleRate = newSampleRate;
        patchState.blockSize = blockSize;
        patchState.blockRate = newSampleRate / blockSize;

        // Destroy and recreate Oneiroi, Clock and UI.
        Oneiroi::destroy(oneiroi_);
        oneiroi_ = Oneiroi::create(&patchCtrls, &patchCvs, &patchState);

        Clock::destroy(clock_);
        clock_ = Clock::create(&patchCtrls, &patchState);

        // Ui is not strongly affected by the sample rate, leave it for now.
    }

    PatchCtrls* getPatchCtrls() { return &patchCtrls; }
    PatchCvs* getPatchCvs() { return &patchCvs; }
    PatchState* getPatchState() { return &patchState; }
    Oneiroi* getOneiroi() { return oneiroi_; }
    Ui* getUi() { return ui_; }
};

} // namespace befacomod
#endif // __Oneiroi_1_2_2Patch_hpp__
