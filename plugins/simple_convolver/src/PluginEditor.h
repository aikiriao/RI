/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
/**
*/
class RIAudioProcessorEditor  : public juce::AudioProcessorEditor
{
public:
    RIAudioProcessorEditor (RIAudioProcessor&);
    ~RIAudioProcessorEditor() override;

    //==============================================================================
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    // This reference is provided as a quick way for your editor to
    // access the processor object that created it.
    RIAudioProcessor& audioProcessor;

    // impulse audio file
    File impulseFile;

    // impulse wav file format manager
    AudioFormatManager formatManager;

    TextButton openButton;
    AudioThumbnailCache thumbnailCache;
    AudioThumbnail thumbnail;

    void openButtonClicked();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RIAudioProcessorEditor)
};
