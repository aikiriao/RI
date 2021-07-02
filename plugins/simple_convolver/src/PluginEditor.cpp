/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
RIAudioProcessorEditor::RIAudioProcessorEditor (RIAudioProcessor& p)
    :   AudioProcessorEditor (&p),
        audioProcessor (p),
        thumbnailCache (5),
        thumbnail (4096, formatManager, thumbnailCache)
{
    // Make sure that before the constructor has finished, you've set the
    // editor's size to whatever you need it to be.
    setSize (300, 150);

    formatManager.registerBasicFormats();
}

RIAudioProcessorEditor::~RIAudioProcessorEditor()
{
}

//==============================================================================
void RIAudioProcessorEditor::paint (juce::Graphics& g)
{
    // (Our component is opaque, so we must completely fill the background with a solid colour)
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));

    addAndMakeVisible (&openButton);
    openButton.setButtonText ("Open impulse");
    openButton.onClick = [this] { openButtonClicked(); };

    juce::Rectangle<int> thumbnailBounds (0, 15, getWidth(), getHeight() - 15);
    g.setColour (juce::Colours::black);
    g.fillRect (thumbnailBounds);

    g.setColour (juce::Colours::white);
    thumbnail.drawChannels (g, thumbnailBounds, 0.0, thumbnail.getTotalLength(), 1.0f);

    g.setFont (15.0f);
    g.drawText (impulseFile.getFileName(), getWidth() / 4, 0, (getWidth() * 3) / 4, 15, Justification::right);
}

void RIAudioProcessorEditor::resized()
{
    openButton.setBounds (0, 0, getWidth() / 4, 15);
}

void RIAudioProcessorEditor::openButtonClicked()
{
    juce::FileChooser chooser ("Select a Impulse Wav File.", {}, "*.wav");

    if (chooser.browseForFileToOpen())
    {
        auto file = chooser.getResult();
        auto* reader = formatManager.createReaderFor (file);

        if (reader != nullptr)
        {
            const int numChannels = static_cast<int>(reader->numChannels);
            const int numSamples = static_cast<int>(reader->lengthInSamples);
            AudioBuffer<float> audioBuffer(numChannels, numSamples);
            impulseFile = file;
            reader->read(&audioBuffer, 0, numSamples, 0, false, false);
            audioProcessor.setImpulse(audioBuffer.getArrayOfReadPointers(),
                    static_cast<uint32_t>(numChannels), static_cast<uint32_t>(numSamples));
            thumbnail.setSource (new juce::FileInputSource (file));
            delete reader;
            repaint();
        }
    }
}
