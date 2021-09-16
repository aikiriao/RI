/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ri_zerolatency_fft_convolve.h"

#include <cstring>

namespace {
    // デフォルトのインパルス
    const float defaultImpulse[] = { 1.0f, 0.0f, 0.0f, 0.0f };
    const float *pdefaultImpulse[] = { defaultImpulse, defaultImpulse };
}

//==============================================================================
RIAudioProcessor::RIAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
     : AudioProcessor (BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
                      #if ! JucePlugin_IsSynth
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      #endif
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                     #endif
                       )
#endif
{
    const uint32_t defaultNumChannels = sizeof(pdefaultImpulse) / sizeof(pdefaultImpulse[0]);
    const uint32_t defaultImpulseLength = sizeof(defaultImpulse) / sizeof(defaultImpulse[0]);

    // インターフェース取得
    convInterface = RIZeroLatencyFFTConvolve_GetInterface();

    // 畳み込みオブジェクト作成
    {
        convConfig.max_num_input_samples = 512; // PrepareToPlayが実行されるまでの仮値
        convConfig.max_num_coefficients = defaultImpulseLength;
        convWorkSize = convInterface->CalculateWorkSize(&convConfig);
        convWork = new uint8_t*[defaultNumChannels];
        conv = new void*[defaultNumChannels];
        for (uint32_t channel = 0; channel < defaultNumChannels; channel++) {
            convWork[channel] = new uint8_t[static_cast<size_t>(convWorkSize)];
            conv[channel] = convInterface->Create(&convConfig, convWork[channel], convWorkSize);
        }
    }

    // 信号処理バッファ
    pcm_buffer = new float[convConfig.max_num_input_samples];

    // インパルス信号記録領域
    impulse = new float*[defaultNumChannels];
    for (uint32_t channel = 0; channel < defaultNumChannels; channel++) {
        impulse[channel] = new float[defaultImpulseLength];
    }

    // 仮のインパルスを設定
    channelCounts = defaultNumChannels;
    impulseLength = defaultImpulseLength;
    setImpulse(pdefaultImpulse, channelCounts, impulseLength);
}

RIAudioProcessor::~RIAudioProcessor()
{
    // インパルスの破棄
    for (uint32_t channel = 0; channel < channelCounts; channel++) {
        delete[] impulse[channel];
    }
    delete[] impulse;

    // 信号処理バッファの破棄
    delete[] pcm_buffer;

    // 畳み込みオブジェクトの破棄
    for (uint32_t channel = 0; channel < channelCounts; channel++) {
        convInterface->Destroy(conv[channel]);
        delete[] convWork[channel];
    }
    delete[] convWork;
    delete[] conv;

    convInterface = nullptr;
}

//==============================================================================
const juce::String RIAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool RIAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool RIAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool RIAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double RIAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int RIAudioProcessor::getNumPrograms()
{
    return 1;   // NB: some hosts don't cope very well if you tell them there are 0 programs,
                // so this should be at least 1, even if you're not really implementing programs.
}

int RIAudioProcessor::getCurrentProgram()
{
    return 0;
}

void RIAudioProcessor::setCurrentProgram (int index)
{
    ignoreUnused (index);
}

const juce::String RIAudioProcessor::getProgramName (int index)
{
    ignoreUnused (index);
    return {};
}

void RIAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
    ignoreUnused (index);
    ignoreUnused (newName);
}

//==============================================================================
void RIAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    ignoreUnused (sampleRate);

    // 入力サンプル数が変わった場合はインスタンスを作り直す
    if (convConfig.max_num_input_samples != static_cast<uint32_t>(samplesPerBlock))
    {
        convLock.enter();

        convConfig.max_num_input_samples = static_cast<uint32_t>(samplesPerBlock);
        convWorkSize = convInterface->CalculateWorkSize(&convConfig);
        for (uint32_t channel = 0; channel < channelCounts; channel++)
        {
            convInterface->Destroy(conv[channel]);
            delete[] convWork[channel];
            convWork[channel] = new uint8_t[static_cast<size_t>(convWorkSize)];
            conv[channel] = convInterface->Create(&convConfig, convWork[channel], convWorkSize);
        }

        // 信号処理バッファ再度割当
        delete[] pcm_buffer;
        pcm_buffer = new float[convConfig.max_num_input_samples];

        convLock.exit();

        // インパルスも再設定
        setImpulse((const float **)impulse, channelCounts, impulseLength);
    }

}

void RIAudioProcessor::releaseResources()
{
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool RIAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    ignoreUnused (layouts);
    return true;
  #else
    // This is the place where you check if the layout is supported.
    // In this template code we only support mono or stereo.
    // Some plugin hosts, such as certain GarageBand versions, will only
    // load plugins that support stereo bus layouts.
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    // This checks if the input layout matches the output layout
   #if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
   #endif

    return true;
  #endif
}
#endif

void RIAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    ScopedNoDenormals noDenormals;
    int totalNumInputChannels = getTotalNumInputChannels();
    int totalNumOutputChannels = getTotalNumOutputChannels();
    int processChannels = jmin(static_cast<int>(channelCounts), totalNumInputChannels);
    int processSamples = buffer.getNumSamples();

    ignoreUnused (midiMessages);

    for (int i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, processSamples);

    convLock.enter();
    for (int channel = 0; channel < processChannels; ++channel)
    {
        auto* input = buffer.getReadPointer (channel);
        auto* output = buffer.getWritePointer (channel);
        // inputとoutputが同じ領域を指している場合があるのでバッファにコピー
        memcpy(pcm_buffer, input, sizeof(float) * static_cast<size_t>(processSamples));
        convInterface->Convolve(conv[channel], pcm_buffer, output, static_cast<uint32_t>(processSamples));
    }
    convLock.exit();
}

//==============================================================================
bool RIAudioProcessor::hasEditor() const
{
    return true; // (change this to false if you choose to not supply an editor)
}

juce::AudioProcessorEditor* RIAudioProcessor::createEditor()
{
    return new RIAudioProcessorEditor (*this);
}

//==============================================================================
void RIAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    ignoreUnused (destData);
}

void RIAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    ignoreUnused (data);
    ignoreUnused (sizeInBytes);
}

// インパルスの設定
void RIAudioProcessor::setImpulse (const float** impulse, uint32_t channelCounts, uint32_t impulseLength)
{
    convLock.enter();

    // インスタンスを破棄
    for (uint32_t channel = 0; channel < this->channelCounts; channel++) {
        convInterface->Destroy(conv[channel]);
        delete[] convWork[channel];
    }
    delete[] convWork;
    delete[] conv;

    // 記録してあったインパルスを破棄
    if (impulse != this->impulse) {
        for (uint32_t channel = 0; channel < this->channelCounts; channel++) {
            delete[] this->impulse[channel];
        }
        delete[] this->impulse;
    }

    // 現在のインパルス情報を記録
    this->channelCounts = channelCounts;
    this->impulseLength = impulseLength;

    // インスタンスを再度作成
    convConfig.max_num_coefficients = impulseLength;
    convWorkSize = convInterface->CalculateWorkSize(&convConfig);
    convWork = new uint8_t*[channelCounts];
    conv = new void*[channelCounts];
    for (uint32_t channel = 0; channel < channelCounts; channel++) {
        convWork[channel] = new uint8_t[static_cast<size_t>(convWorkSize)];
        conv[channel] = convInterface->Create(&convConfig, convWork[channel], convWorkSize);
        jassert(conv[channel] != NULL);
    }

    // インパルスを記録
    if (impulse != this->impulse) {
        this->impulse = new float*[channelCounts];
        for (uint32_t channel = 0; channel < channelCounts; channel++) {
            this->impulse[channel] = new float[impulseLength];
            memcpy(this->impulse[channel], impulse[channel], sizeof(float) * impulseLength);
        }
    }

    // インパルス設定
    for (uint32_t channel = 0; channel < channelCounts; channel++) {
        convInterface->SetCoefficients(conv[channel], impulse[channel], impulseLength);
    }

    convLock.exit();
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new RIAudioProcessor();
}
