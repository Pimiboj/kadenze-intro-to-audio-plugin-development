/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
KadenzeChorusFlangerAudioProcessor::KadenzeChorusFlangerAudioProcessor()
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
    /* Construct  and add parameters*/
    addParameter(mDryWetParameter = new juce::AudioParameterFloat("drywet",
        "Dry Wet",
        0.0f,
        1.0f,
        0.5f));

    addParameter(mDepthParameter = new juce::AudioParameterFloat("depth",
        "Depth",
        0.0f,
        1.0f,
        0.5f));

    addParameter(mRateParameter = new juce::AudioParameterFloat("rate",
        "Rate",
        0.1f,
        20.0f,
        10.0f));

    addParameter(mPhaseOffsetParameter = new juce::AudioParameterFloat("phaseoffset",
        "Phase Offset",
        0.0f,
        1.0f,
        0.0f));

    addParameter(mFeedbackParameter = new juce::AudioParameterFloat("feedback",
        "Feedback",
        0.0f,
        0.98f,
        0.5f));

    addParameter(mTypeParameter = new juce::AudioParameterInt("type",
        "Type",
        0,
        1,
        0));

    /* Initialize to default values */
    mCircularBufferWriteHead = 0;
    mCircularBufferLength = 0;
    mFeedbackLeft = 0;
    mFeedbackRight = 0;
    mLFOPhase = 0;
}

KadenzeChorusFlangerAudioProcessor::~KadenzeChorusFlangerAudioProcessor()
{
}

//==============================================================================
const juce::String KadenzeChorusFlangerAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool KadenzeChorusFlangerAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool KadenzeChorusFlangerAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool KadenzeChorusFlangerAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double KadenzeChorusFlangerAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int KadenzeChorusFlangerAudioProcessor::getNumPrograms()
{
    return 1;   // NB: some hosts don't cope very well if you tell them there are 0 programs,
                // so this should be at least 1, even if you're not really implementing programs.
}

int KadenzeChorusFlangerAudioProcessor::getCurrentProgram()
{
    return 0;
}

void KadenzeChorusFlangerAudioProcessor::setCurrentProgram (int index)
{
}

const juce::String KadenzeChorusFlangerAudioProcessor::getProgramName (int index)
{
    return {};
}

void KadenzeChorusFlangerAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
}

//==============================================================================
void KadenzeChorusFlangerAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    /* Initialize our data for the current sample rate, and reset phase and writeheads */

    /* Initiialize phase */
    mLFOPhase = 0;

    /* Calculate the circular buffer length */
    mCircularBufferLength = sampleRate * MAX_DELAY_TIME;

    /* Initialize the buffers*/
    mCircularBufferLeft.reset(new float[mCircularBufferLength]);
    mCircularBufferRight.reset(new float[mCircularBufferLength]);

    /* Clear the data in the buffers */
    for (int i = 0; i < mCircularBufferLength; i++)
    {
        mCircularBufferLeft[i] = 0.0f;
        mCircularBufferRight[i] = 0.0f;
    }

    /* Initialize the write head to 0 */
    mCircularBufferWriteHead = 0;
}

void KadenzeChorusFlangerAudioProcessor::releaseResources()
{
    // When playback stops, you can use this as an opportunity to free up any
    // spare memory, etc.
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool KadenzeChorusFlangerAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
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

void KadenzeChorusFlangerAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    auto totalNumInputChannels = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    DBG("DRY WET: " << * mDryWetParameter);
    DBG("DEPTH: " << *mDepthParameter);
    DBG("RATE: " << *mRateParameter);
    DBG("PHASE OFFSET: " << *mPhaseOffsetParameter);
    DBG("FEEDBACK: " << *mFeedbackParameter);
    DBG("TYPE: " << (int)* mTypeParameter);

    // In case we have more outputs than inputs, this code clears any output
    // channels that didn't contain input data, (because these aren't
    // guaranteed to be empty - they may contain garbage).
    // This is here to avoid people getting screaming feedback
    // when they first compile a plugin, but obviously you don't need to keep
    // this code if your algorithm always overwrites all the output channels.
    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear(i, 0, buffer.getNumSamples());

    /* Obtain left and right audio data pointers */
    float* leftChannel = buffer.getWritePointer(0);
    float* rightChannel = buffer.getWritePointer(1);

    /* Iterate through all samples in the buffer */
    for (int i = 0; i < buffer.getNumSamples(); i++)
    {
        /* Write into circular buffer */
        mCircularBufferLeft[mCircularBufferWriteHead] = leftChannel[i] + mFeedbackLeft;
        mCircularBufferRight[mCircularBufferWriteHead] = rightChannel[i] + mFeedbackRight;

        /* Generate left lfo output */
        float lfoOutLeft = sin(2 * juce::MathConstants<float>().pi * mLFOPhase);

        /* Calculate right channel lfo phase */
        float lfoPhaseRight = mLFOPhase + *mPhaseOffsetParameter;
        if (lfoPhaseRight > 1)
        {
            lfoPhaseRight -= 1;
        }

        /* Generate right lfo output */
        float lfoOutRight = sin(2 * juce::MathConstants<float>().pi * lfoPhaseRight);

        /* Move LFO phase forward */
        mLFOPhase += *mRateParameter / getSampleRate();
        if (mLFOPhase > 1)
        {
            mLFOPhase -= 1;
        }

        /* Control the lfo depth */
        lfoOutLeft *= *mDepthParameter;
        lfoOutRight *= *mDepthParameter;

        float lfoOutMappedLeft = 0;
        float lfoOutMappedRight = 0;

        /* Map the lfo output to the desired delay times */
        /* Chorus */
        if (*mTypeParameter == 0)
        {
            lfoOutMappedLeft = juce::jmap(lfoOutLeft, -1.0f, 1.0f, 0.005f, 0.030f);
            lfoOutMappedRight = juce::jmap(lfoOutRight, -1.0f, 1.0f, 0.005f, 0.030f);
        }
        /* Flanger */
        else
        {
            lfoOutMappedLeft = juce::jmap(lfoOutLeft, -1.0f, 1.0f, 0.001f, 0.005f);
            lfoOutMappedRight = juce::jmap(lfoOutRight, -1.0f, 1.0f, 0.001f, 0.005f);
        }

        /* Calulate the delay length in samples */
        float delayTimeSamplesLeft = getSampleRate() * lfoOutMappedLeft;
        float delayTimeSamplesRight = getSampleRate() * lfoOutMappedRight;

        /* Calculare left read head */
        float delayReadHeadLeft = mCircularBufferWriteHead - delayTimeSamplesLeft;
        if (delayReadHeadLeft < 0)
        {
            delayReadHeadLeft += mCircularBufferLength;
        }

        /* Calculare right read head */
        float delayReadHeadRight = mCircularBufferWriteHead - delayTimeSamplesRight;
        if (delayReadHeadRight < 0)
        {
            delayReadHeadRight += mCircularBufferLength;
        }

        /* Calculate linear interpolation points for left channel */
        int readHeadLeft_x0 = (int)delayReadHeadLeft;
        int readHeadLeft_x1 = readHeadLeft_x0 + 1;
        float readHeadFloatLeft = delayReadHeadLeft - readHeadLeft_x0;
        if (readHeadLeft_x1 >= mCircularBufferLength)
        {
            readHeadLeft_x1 -= mCircularBufferLength;
        }

        /* Calculate linear interpolation points for right channel */
        int readHeadRight_x0 = (int)delayReadHeadRight;
        int readHeadRight_x1 = readHeadRight_x0 + 1;
        float readHeadFloatRight = delayReadHeadRight - readHeadRight_x0;
        if (readHeadRight_x1 >= mCircularBufferLength)
        {
            readHeadRight_x1 -= mCircularBufferLength;
        }

        /* Generate left and right output samples */
        float delay_sample_left = lin_interp(mCircularBufferLeft[readHeadLeft_x0], mCircularBufferLeft[readHeadLeft_x1], readHeadFloatLeft);
        float delay_sample_right = lin_interp(mCircularBufferRight[readHeadRight_x0], mCircularBufferRight[readHeadRight_x1], readHeadFloatRight);

        mFeedbackLeft = delay_sample_left * *mFeedbackParameter;
        mFeedbackRight = delay_sample_right * *mFeedbackParameter;

        /* Increment cirular buffer write head */
        mCircularBufferWriteHead++;

        if (mCircularBufferWriteHead >= mCircularBufferLength)
        {
            mCircularBufferWriteHead = 0;
        }

        /* Add feedback to the output according to the dry wet parameter */
        float dryAmount = 1 - *mDryWetParameter;
        float wetAmount = *mDryWetParameter;

        buffer.setSample(0, i, buffer.getSample(0, i) * dryAmount + delay_sample_left * wetAmount);
        buffer.setSample(1, i, buffer.getSample(1, i) * dryAmount + delay_sample_right * wetAmount);
    }
}

//==============================================================================
bool KadenzeChorusFlangerAudioProcessor::hasEditor() const
{
    return true; // (change this to false if you choose to not supply an editor)
}

juce::AudioProcessorEditor* KadenzeChorusFlangerAudioProcessor::createEditor()
{
    return new KadenzeChorusFlangerAudioProcessorEditor (*this);
}

//==============================================================================
void KadenzeChorusFlangerAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    std::unique_ptr<juce::XmlElement> xml(new juce::XmlElement("FlangerChorus"));

    xml->setAttribute("DryWet", *mDryWetParameter);
    xml->setAttribute("Depth", *mDepthParameter);
    xml->setAttribute("Rate", *mRateParameter);
    xml->setAttribute("PhaseOffset", *mPhaseOffsetParameter);
    xml->setAttribute("Feedback", *mFeedbackParameter);
    xml->setAttribute("Type", *mTypeParameter);

    copyXmlToBinary(*xml, destData);
}

void KadenzeChorusFlangerAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));

    if (xml.get() != nullptr && xml->hasTagName("FlangerChorus"))
    {
        *mDryWetParameter = xml->getDoubleAttribute("DryWet");
        *mDepthParameter = xml->getDoubleAttribute("Depth");
        *mRateParameter = xml->getDoubleAttribute("Rate");
        *mPhaseOffsetParameter = xml->getDoubleAttribute("PhaseOffset");
        *mFeedbackParameter = xml->getDoubleAttribute("Feedback");

        *mTypeParameter = xml->getIntAttribute("Type");
    }
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new KadenzeChorusFlangerAudioProcessor();
}

float KadenzeChorusFlangerAudioProcessor::lin_interp(float sample_x0, float sample_x1, float inPhase)
{
    return (1 - inPhase) * sample_x0 + inPhase * sample_x1;
}
