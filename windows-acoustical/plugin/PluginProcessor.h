// PluginProcessor.h — "Acoustical Dynamic EQ": el mismo motor de la app
// (análisis + corrector + los tres ecuas dinámicos idénticos con distintos
// ajustes) dentro de Cubase 5, como VST3 / VST2.4.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <AcousticalEngine.h>

class AcousticalAudioProcessor : public juce::AudioProcessor {
public:
    AcousticalAudioProcessor();

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Acoustical Dynamic EQ"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "Default"; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    acoustical::AcousticalEngine& engine() { return engine_; }

    // Copia thread-safe del último análisis para el editor
    void getLatestAnalysis(acoustical::AnalysisResult& out,
                           acoustical::SweepStep sweepsOut[3]) const {
        std::lock_guard<std::mutex> lock(mutex_);
        if (latest_) out = *latest_;
        for (int i = 0; i < 3; ++i) sweepsOut[i] = sweepSteps_[i];
    }

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void applyParameters();

private:
    void analyzeLatest(const juce::AudioBuffer<float>& buffer);

    acoustical::AcousticalEngine engine_;
    juce::AudioProcessorValueTreeState apvts;

    mutable std::mutex mutex_;
    std::unique_ptr<acoustical::AnalysisResult> latest_;
    acoustical::SweepStep sweepSteps_[3]{};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AcousticalAudioProcessor)
};
