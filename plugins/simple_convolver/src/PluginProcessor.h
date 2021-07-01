/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "ri_convolve.h"

//==============================================================================
/**
*/
class RIAudioProcessor  : public juce::AudioProcessor
{
public:
    //==============================================================================
    RIAudioProcessor();
    ~RIAudioProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;
    
    // インパルスの設定
    void setImpulse (const float** inpulse, uint32_t channelCounts, uint32_t sampleCounts);

private:
    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RIAudioProcessor)

    void **conv;
    uint8_t **convWork;
    int32_t convWorkSize;
    const RIConvolveInterface *convInterface;
    struct RIConvolveConfig convConfig;
    CriticalSection convLock;
    float *pcm_buffer;
    float **impulse;
    uint32_t channelCounts, impulseLength;
};
