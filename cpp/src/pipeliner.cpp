//
//  pipeliner.cpp
//  ndnrtc
//
//  Copyright 2013 Regents of the University of California
//  For licensing details see the LICENSE file.
//
//  Author:  Peter Gusev
//

#include "pipeliner.h"
#include "ndnrtc-namespace.h"
#include "rtt-estimation.h"

using namespace std;
using namespace webrtc;
using namespace ndnrtc;
using namespace ndnrtc::new_api;
using namespace ndnlog;

const double Pipeliner::SegmentsAvgNumDelta = 8.;
const double Pipeliner::SegmentsAvgNumKey = 25.;
const double Pipeliner::ParitySegmentsAvgNumDelta = 2.;
const double Pipeliner::ParitySegmentsAvgNumKey = 5.;
const int64_t Pipeliner::MaxInterruptionDelay = 1000;

//******************************************************************************
#pragma mark - construction/destruction
ndnrtc::new_api::Pipeliner::Pipeliner(const shared_ptr<Consumer> &consumer):
NdnRtcObject(consumer->getParameters()),
consumer_(consumer),
isProcessing_(false),
mainThread_(*ThreadWrapper::CreateThread(Pipeliner::mainThreadRoutine, this)),
isPipelining_(false),
isPipelinePaused_(false),
isBuffering_(false),
pipelineIntervalMs_(0),
pipelineThread_(*ThreadWrapper::CreateThread(Pipeliner::pipelineThreadRoutin, this)),
pipelineTimer_(*EventWrapper::Create()),
pipelinerPauseEvent_(*EventWrapper::Create()),
deltaSegnumEstimatorId_(NdnRtcUtils::setupMeanEstimator(0, SegmentsAvgNumDelta)),
keySegnumEstimatorId_(NdnRtcUtils::setupMeanEstimator(0, SegmentsAvgNumKey)),
deltaParitySegnumEstimatorId_(NdnRtcUtils::setupMeanEstimator(0, ParitySegmentsAvgNumDelta)),
keyParitySegnumEstimatorId_(NdnRtcUtils::setupMeanEstimator(0, ParitySegmentsAvgNumKey)),
rtxFreqMeterId_(NdnRtcUtils::setupFrequencyMeter()),
exclusionFilter_(-1),
useKeyNamespace_(true)
{
    initialize();
}

ndnrtc::new_api::Pipeliner::~Pipeliner()
{
    
}

//******************************************************************************
#pragma mark - public
int
ndnrtc::new_api::Pipeliner::start()
{
    bufferEventsMask_ = ndnrtc::new_api::FrameBuffer::Event::StateChanged;
    isProcessing_ = true;
    rebufferingNum_ = 0;
    
    unsigned int tid;
    mainThread_.Start(tid);
    
    LogInfoC << "started" << endl;
    return RESULT_OK;
}

int
ndnrtc::new_api::Pipeliner::stop()
{
    if (isPipelining_)
        stopChasePipeliner();
    
    frameBuffer_->release();
    mainThread_.SetNotAlive();
    mainThread_.Stop();
    
    LogInfoC << "stopped" << endl;
    return RESULT_OK;
}

Pipeliner::State
Pipeliner::getState() const
{
    if (isProcessing_)
    {
        if (isBuffering_)
            return StateBuffering;
        else
        {
            if (frameBuffer_->getState() == FrameBuffer::Invalid)
                return StateChasing;
            else
                return StateFetching;
        }
    }
    else
        return StateInactive;
}

//******************************************************************************
#pragma mark - static

//******************************************************************************
#pragma mark - private
bool
ndnrtc::new_api::Pipeliner::processEvents()
{
    ndnrtc::new_api::FrameBuffer::Event
    event = frameBuffer_->waitForEvents(bufferEventsMask_);
    
    LogTraceC << "event " << FrameBuffer::Event::toString(event) << endl;
    
    switch (event.type_) {
        case FrameBuffer::Event::Error : {
            
            LogErrorC << "error on buffer events" << endl;
            
            isProcessing_ = false;
        }
            break;
        case FrameBuffer::Event::FirstSegment:
        {
            updateSegnumEstimation(event.slot_->getNamespace(),
                                   event.slot_->getSegmentsNumber(),
                                   false);
            updateSegnumEstimation(event.slot_->getNamespace(),
                                   event.slot_->getParitySegmentsNumber(),
                                   true);
        } // fall through
        default:
        {
            if (frameBuffer_->getState() == FrameBuffer::Valid)
                handleValidState(event);
            else
                handleInvalidState(event);
        }
            break;
    }
    
    recoveryCheck(event);
    
    return isProcessing_;
}

int
ndnrtc::new_api::Pipeliner::handleInvalidState
(const ndnrtc::new_api::FrameBuffer::Event &event)
{
    LogTraceC << "invalid buffer state" << endl;
    
    unsigned int activeSlotsNum = frameBuffer_->getActiveSlotsNum();
    
    if (activeSlotsNum == 0)
    {
        shared_ptr<Interest>
        rightmost = getInterestForRightMost(getInterestLifetime(params_.interestTimeout),
                                            false, exclusionFilter_);
        
        express(*rightmost, getInterestLifetime(params_.interestTimeout));
        bufferEventsMask_ = FrameBuffer::Event::FirstSegment |
                            FrameBuffer::Event::Timeout;
        recoveryCheckpointTimestamp_ = NdnRtcUtils::millisecondTimestamp();
    }
    else
        handleChase(event);
    
    
    return RESULT_OK;
}

int
ndnrtc::new_api::Pipeliner::handleChase(const FrameBuffer::Event &event)
{
    switch (event.type_) {
        case FrameBuffer::Event::Timeout:
            handleTimeout(event);
            break;
            
        case FrameBuffer::Event::FirstSegment: //
        {
            requestMissing(event.slot_,
                           getInterestLifetime(event.slot_->getPlaybackDeadline(),
                                               event.slot_->getNamespace()),
                           0);
        } // fall through
        default:
        {
            unsigned int activeSlotsNum = frameBuffer_->getActiveSlotsNum();
            
            if (activeSlotsNum == 1)
                initialDataArrived(event.slot_);
            
            if (activeSlotsNum > 1)
            {
                if (isBuffering_)
                    handleBuffering(event);
                else
                    chaseDataArrived(event);
            }
        }
            break;
    }
    
    return RESULT_OK;
}

void
ndnrtc::new_api::Pipeliner::initialDataArrived
(const shared_ptr<ndnrtc::new_api::FrameBuffer::Slot>& slot)
{
    LogTraceC << "first data arrived " << slot->getPrefix() << endl;
    
    // request next key frame
    keyFrameSeqNo_ = slot->getPairedFrameNumber()+1;
    requestNextKey(keyFrameSeqNo_);
    
    double producerRate = (slot->getPacketRate()>0)?slot->getPacketRate():params_.producerRate;
    bufferEstimator_->setProducerRate(producerRate);
    startChasePipeliner(slot->getSequentialNumber()+1, 1000./(2*producerRate));
    
    bufferEventsMask_ = FrameBuffer::Event::FirstSegment |
                        FrameBuffer::Event::Ready |
                        FrameBuffer::Event::Timeout |
                        FrameBuffer::Event::StateChanged |
                        FrameBuffer::Event::NeedKey;
}

void
ndnrtc::new_api::Pipeliner::chaseDataArrived(const FrameBuffer::Event& event)
{
    switch (event.type_) {
        case FrameBuffer::Event::NeedKey:
        {
            requestNextKey(keyFrameSeqNo_);
        }
            break;
        case FrameBuffer::Event::FirstSegment:
        {
            LogTraceC << "chaser track " << endl;
            
            chaseEstimation_->trackArrival();
        } // fall through
        default:
        {
            pipelineIntervalMs_ = chaseEstimation_->getArrivalEstimation();

            int bufferSize = frameBuffer_->getEstimatedBufferSize();
            assert(bufferSize >= 0);
            
            LogTraceC
            << "buffer size " << bufferSize
            << " target " << frameBuffer_->getTargetSize() << endl;
            
            if (chaseEstimation_->isArrivalStable())
            {
                LogTraceC
                << "[****] arrival stable -> buffering. buf size "
                << bufferSize << endl;
                
                stopChasePipeliner();
                frameBuffer_->setTargetSize(bufferEstimator_->getTargetSize());
                isBuffering_ = true;
            }
            else
            {
                if (bufferSize >
                    frameBuffer_->getTargetSize())
                {
                    LogTraceC << "buffer is too large - recycle old frames" << endl;
                    
                    setChasePipelinerPaused(true);
                    frameBuffer_->recycleOldSlots();
                    
                    bufferSize = frameBuffer_->getEstimatedBufferSize();
                    
                    LogTraceC << "new buffer size " << bufferSize << endl;
                }
                else
                {
                    if (isPipelinePaused_)
                    {
                        LogTraceC << "resuming chase pipeliner" << endl;
                        
                        setChasePipelinerPaused(false);
                    }
                }
            }
        }
            break;
    }
}

void
ndnrtc::new_api::Pipeliner::handleBuffering(const FrameBuffer::Event& event)
{
    int bufferSize = frameBuffer_->getPlayableBufferSize();
    
    LogTraceC << "buffering. playable size " << bufferSize << endl;
    
    if (bufferSize >= frameBuffer_->getTargetSize()*2./3.)
    {
        LogTraceC
        << "[*****] switch to valid state. playable size " << bufferSize << endl;
        
        isBuffering_ = false;
        frameBuffer_->setState(FrameBuffer::Valid);
        bufferEventsMask_ |= FrameBuffer::Event::Playout;
    }
    else
    {
        keepBuffer();
        
        LogTraceC << "[*****] buffering. playable size " << bufferSize << endl;
        
        frameBuffer_->dump();
    }
}

int
ndnrtc::new_api::Pipeliner::handleValidState
(const ndnrtc::new_api::FrameBuffer::Event &event)
{
    int bufferSize = frameBuffer_->getEstimatedBufferSize();
    
    LogTraceC
    << "valid buffer state. "
    << "buf size " << bufferSize
    << " target " << frameBuffer_->getTargetSize() << endl;
    
    switch (event.type_) {
        case FrameBuffer::Event::FirstSegment:
        {
            requestMissing(event.slot_,
                           getInterestLifetime(event.slot_->getPlaybackDeadline(),
                                               event.slot_->getNamespace()),
                           event.slot_->getPlaybackDeadline());
        }
            break;
            
        case FrameBuffer::Event::Timeout:
        {
            handleTimeout(event);
        }
            break;
        case FrameBuffer::Event::Ready:
        {
            LogStatC << "ready\t" << event.slot_->dump() << endl;
        }
            break;
        case FrameBuffer::Event::NeedKey:
        {
            requestNextKey(keyFrameSeqNo_);
        }
            break;
        case FrameBuffer::Event::Playout:
        {
        }
            break;
        default:
            break;
    }
    
    keepBuffer();
    
    return RESULT_OK;
}

void
ndnrtc::new_api::Pipeliner::handleTimeout(const FrameBuffer::Event &event)
{
    LogTraceC
    << "timeout " << event.slot_->getPrefix()
    << " deadline " << event.slot_->getPlaybackDeadline() << endl;

    if (params_.useRtx)
        requestMissing(event.slot_,
                       getInterestLifetime(event.slot_->getPlaybackDeadline(),
                                           event.slot_->getNamespace(),
                                           true),
                       event.slot_->getPlaybackDeadline(), true);
}

int
ndnrtc::new_api::Pipeliner::initialize()
{
    params_ = consumer_->getParameters();
    frameBuffer_ = consumer_->getFrameBuffer();
    chaseEstimation_ = consumer_->getChaseEstimation();
    bufferEstimator_ = consumer_->getBufferEstimator();
    ndnAssembler_ = consumer_->getPacketAssembler();
    
    shared_ptr<string>
    streamPrefixString = NdnRtcNamespace::getStreamPrefix(params_);
    
    shared_ptr<string>
    deltaPrefixString = NdnRtcNamespace::getStreamFramePrefix(params_);
    
    shared_ptr<string>
    keyPrefixString = NdnRtcNamespace::getStreamFramePrefix(params_, true);
    
    if (!streamPrefixString.get() || !deltaPrefixString.get() ||
        !keyPrefixString.get())
        return notifyError(RESULT_ERR, "producer frames prefixes \
                           were not provided");

    streamPrefix_ = Name(streamPrefixString->c_str());
    deltaFramesPrefix_ = Name(deltaPrefixString->c_str());
    keyFramesPrefix_ = Name(keyPrefixString->c_str());
    
    return RESULT_OK;
}

shared_ptr<Interest>
ndnrtc::new_api::Pipeliner::getDefaultInterest(const ndn::Name &prefix, int64_t timeoutMs)
{
    shared_ptr<Interest> interest(new Interest(prefix, (timeoutMs == 0)?consumer_->getParameters().interestTimeout:timeoutMs));
    interest->setMustBeFresh(true);
    
    return interest;
}

shared_ptr<Interest>
ndnrtc::new_api::Pipeliner::getInterestForRightMost(int64_t timeoutMs,
                                                    bool isKeyNamespace,
                                                    PacketNumber exclude)
{
    Name prefix = isKeyNamespace?keyFramesPrefix_:deltaFramesPrefix_;
    shared_ptr<Interest> rightmost = getDefaultInterest(prefix, timeoutMs);
    
    rightmost->setChildSelector(1);
    rightmost->setMinSuffixComponents(2);
    
    if (exclude >= 0)
    {
        rightmost->getExclude().appendAny();
        rightmost->getExclude().appendComponent(NdnRtcUtils::componentFromInt(exclude));
    }
    
    return rightmost;
}

void
ndnrtc::new_api::Pipeliner::updateSegnumEstimation(FrameBuffer::Slot::Namespace frameNs,
                                                   int nSegments, bool isParity)
{
    int estimatorId = 0;
    
    if (isParity)
    {
        estimatorId = (frameNs == FrameBuffer::Slot::Key)?
    keyParitySegnumEstimatorId_:deltaParitySegnumEstimatorId_;
    }
    else
    {
        estimatorId = (frameNs == FrameBuffer::Slot::Key)?
    keySegnumEstimatorId_:deltaSegnumEstimatorId_;
    }
    
    NdnRtcUtils::meanEstimatorNewValue(estimatorId, (double)nSegments);
}

void
ndnrtc::new_api::Pipeliner::requestNextKey(PacketNumber& keyFrameNo)
{
    // just ignore if key namespace is not used
    if (!useKeyNamespace_)
        return;
        
    LogTraceC << "request key " << keyFrameNo << endl;
    
    prefetchFrame(keyFramesPrefix_,
                  keyFrameNo++,
                  ceil(NdnRtcUtils::currentMeanEstimation(keySegnumEstimatorId_))-1,
                  ceil(NdnRtcUtils::currentMeanEstimation(keyParitySegnumEstimatorId_))-1,
                  FrameBuffer::Slot::Key);
}

void
ndnrtc::new_api::Pipeliner::requestNextDelta(PacketNumber& deltaFrameNo)
{
    prefetchFrame(deltaFramesPrefix_,
                  deltaFrameNo++,
                  ceil(NdnRtcUtils::currentMeanEstimation(deltaSegnumEstimatorId_))-1,
                  ceil(NdnRtcUtils::currentMeanEstimation(deltaParitySegnumEstimatorId_))-1);
}

void
ndnrtc::new_api::Pipeliner::expressRange(Interest& interest,
                                         SegmentNumber startNo,
                                         SegmentNumber endNo,
                                         int64_t priority, bool isParity)
{
    Name prefix = interest.getName();
    
    LogTraceC
    << "express range "
    << interest.getName() << ((isParity)?"/parity":"/data")
    << "/[" << startNo << ", " << endNo << "]"<< endl;
    
    std::vector<shared_ptr<Interest>> segmentInterests;
    frameBuffer_->interestRangeIssued(interest, startNo, endNo, segmentInterests, isParity);
    
    if (segmentInterests.size())
    {
        std::vector<shared_ptr<Interest>>::iterator it;
        for (it = segmentInterests.begin(); it != segmentInterests.end(); ++it)
        {
            shared_ptr<Interest> interestPtr = *it;
            
            consumer_->getInterestQueue()->enqueueInterest(*interestPtr,
                                                               Priority::fromArrivalDelay(priority),
                                                               ndnAssembler_->getOnDataHandler(),
                                                               ndnAssembler_->getOnTimeoutHandler());
        }
    }
}

void
ndnrtc::new_api::Pipeliner::express(Interest &interest, int64_t priority)
{
    LogTraceC << "enqueue " << interest.getName() << endl;
    
    frameBuffer_->interestIssued(interest);
    consumer_->getInterestQueue()->enqueueInterest(interest,
                                                       Priority::fromArrivalDelay(priority),
                                                       ndnAssembler_->getOnDataHandler(),
                                                       ndnAssembler_->getOnTimeoutHandler());
}

void
ndnrtc::new_api::Pipeliner::startChasePipeliner(PacketNumber startPacketNo,
                                                 int64_t intervalMs)
{
    assert(intervalMs > 0);
    
    LogTraceC
    << "start pipeline from "
    << startPacketNo << " interval = " << intervalMs << "ms" << endl;
    
    deltaFrameSeqNo_ = startPacketNo;
    pipelineIntervalMs_ = intervalMs;
    isPipelining_ = true;
    
    unsigned int tid;
    pipelineThread_.Start(tid);
}

void
ndnrtc::new_api::Pipeliner::setChasePipelinerPaused(bool isPaused)
{
    isPipelinePaused_ = isPaused;

    if (!isPaused)
        pipelinerPauseEvent_.Set();
}

void
ndnrtc::new_api::Pipeliner::stopChasePipeliner()
{
    isPipelining_ = false;
    isPipelinePaused_ = false;
    pipelineThread_.SetNotAlive();
    pipelinerPauseEvent_.Set();
    pipelineThread_.Stop();
}

bool
ndnrtc::new_api::Pipeliner::processPipeline()
{
    if (isPipelinePaused_)
    {
        LogTraceC << "pipeliner paused" << endl;
        
        pipelinerPauseEvent_.Wait(WEBRTC_EVENT_INFINITE);
        
        LogTraceC << "pipeliner woke up" << endl;
    }
    
    if (isPipelining_)
    {
        if (frameBuffer_->getEstimatedBufferSize() <= frameBuffer_->getTargetSize())
        {
            requestNextDelta(deltaFrameSeqNo_);
        }
        
        pipelineTimer_.StartTimer(false, pipelineIntervalMs_);
        pipelineTimer_.Wait(pipelineIntervalMs_);
    }
    
    return isPipelining_;
}

void
ndnrtc::new_api::Pipeliner::requestMissing
(const shared_ptr<ndnrtc::new_api::FrameBuffer::Slot> &slot,
 int64_t lifetime, int64_t priority, bool wasTimedOut)
{
    // synchronize with buffer
    frameBuffer_->synchronizeAcquire();
    
    vector<shared_ptr<ndnrtc::new_api::FrameBuffer::Slot::Segment>>
    missingSegments = slot->getMissingSegments();
    
    if (missingSegments.size() == 0)
        LogTraceC << "no missing segments for " << slot->getPrefix() << endl;
    
    vector<shared_ptr<ndnrtc::new_api::FrameBuffer::Slot::Segment>>::iterator it;
    for (it = missingSegments.begin(); it != missingSegments.end(); ++it)
    {
        shared_ptr<ndnrtc::new_api::FrameBuffer::Slot::Segment> segment = *it;

        shared_ptr<Interest> segmentInterest;
        
        if (!slot->isRightmost())
        {
            assert((segment->getNumber() >= 0));
            segmentInterest = getDefaultInterest(segment->getPrefix(), lifetime);
        }
        else
            segmentInterest = getInterestForRightMost(lifetime,
                                                      slot->getNamespace()==FrameBuffer::Slot::Key,
                                                      exclusionFilter_);
        
        LogTraceC << "enqueue missing " << segmentInterest->getName() << endl;
        express(*segmentInterest, priority);
        segmentInterest->setInterestLifetimeMilliseconds(lifetime);
        
        if (wasTimedOut)
        {
            slot->incremenrRtxNum();
            NdnRtcUtils::frequencyMeterTick(rtxFreqMeterId_);
            rtxNum_++;
            
            LogStatC << "\tretransmit\t" << rtxNum_ << endl;
        }
    }
    
    frameBuffer_->synchronizeRelease();
}

int64_t
ndnrtc::new_api::Pipeliner::getInterestLifetime(int64_t playbackDeadline,
                                                FrameBuffer::Slot::Namespace nspc,
                                                bool rtx)
{
    int64_t interestLifetime = 0;
    int64_t halfBufferSize = frameBuffer_->getEstimatedBufferSize()/2;
    
    if (playbackDeadline <= 0)
        playbackDeadline = params_.interestTimeout;

    if (halfBufferSize <= 0)
        halfBufferSize = playbackDeadline;
    
    if (rtx || nspc != FrameBuffer::Slot::Key)
    {
        interestLifetime = min(playbackDeadline, halfBufferSize);
    }
    else
    { // only key frames
        double gopInterval = params_.gop/frameBuffer_->getCurrentRate()*1000;
        interestLifetime = gopInterval-2*halfBufferSize;
        
        if (interestLifetime <= 0)
            interestLifetime = params_.interestTimeout;
    }
    
    assert(interestLifetime > 0);
    return interestLifetime;
}

void
ndnrtc::new_api::Pipeliner::prefetchFrame(const ndn::Name &basePrefix,
                                          PacketNumber packetNo,
                                          int prefetchSize, int parityPrefetchSize,
                                          FrameBuffer::Slot::Namespace nspc)
{
    Name packetPrefix(basePrefix);
    packetPrefix.append(NdnRtcUtils::componentFromInt(packetNo));
    
    int64_t playbackDeadline = frameBuffer_->getEstimatedBufferSize();
    shared_ptr<Interest> frameInterest = getDefaultInterest(packetPrefix);
    
    frameInterest->setInterestLifetimeMilliseconds(getInterestLifetime(playbackDeadline, nspc));
    expressRange(*frameInterest,
                 0,
                 prefetchSize,
                 playbackDeadline,
                 false);
    
    if (params_.useFec)
    {
        expressRange(*frameInterest,
                     0,
                     parityPrefetchSize,
                     playbackDeadline,
                     true);
    }
}

void
ndnrtc::new_api::Pipeliner::keepBuffer(bool useEstimatedSize)
{
    int bufferSize = (useEstimatedSize)?frameBuffer_->getEstimatedBufferSize():
    frameBuffer_->getPlayableBufferSize();
    
    LogTraceC
    << "frame buffer " << bufferSize
    << (useEstimatedSize?" (est) ":" (play) ")
    << " target " << frameBuffer_->getTargetSize()
    << ((bufferSize < frameBuffer_->getTargetSize())?" KEEP UP":" NO KEEP UP") << endl;
    
    while (bufferSize < frameBuffer_->getTargetSize())
    {
        LogTraceC
        << "fill up the buffer with " << deltaFrameSeqNo_ << endl;
        
        requestNextDelta(deltaFrameSeqNo_);
        
        bufferSize = (useEstimatedSize)?frameBuffer_->getEstimatedBufferSize():
        frameBuffer_->getPlayableBufferSize();
    }
}

void
ndnrtc::new_api::Pipeliner::resetData()
{
    deltaSegnumEstimatorId_ = NdnRtcUtils::setupMeanEstimator(0, SegmentsAvgNumDelta);
    keySegnumEstimatorId_ = NdnRtcUtils::setupMeanEstimator(0, SegmentsAvgNumKey);
    deltaParitySegnumEstimatorId_ = NdnRtcUtils::setupMeanEstimator(0, ParitySegmentsAvgNumDelta);
    keyParitySegnumEstimatorId_ = NdnRtcUtils::setupMeanEstimator(0, ParitySegmentsAvgNumKey);
    rtxFreqMeterId_ = NdnRtcUtils::setupFrequencyMeter();
    rtxNum_ = 0;
    rtxFreqMeterId_ = NdnRtcUtils::setupFrequencyMeter();
    
    recoveryCheckpointTimestamp_ = -1;
    isPipelinePaused_ = false;
    isPipelining_ = false;
    isBuffering_ = false;
    isProcessing_ = false;
    
    exclusionFilter_ = -1;
    
    frameBuffer_->reset();
}

void
ndnrtc::new_api::Pipeliner::recoveryCheck
(const ndnrtc::new_api::FrameBuffer::Event& event)
{
    if (event.type_ &
        (FrameBuffer::Event::FirstSegment | FrameBuffer::Event::Ready))
    {
        recoveryCheckpointTimestamp_ = NdnRtcUtils::millisecondTimestamp();
    }
    
    if (recoveryCheckpointTimestamp_ > 0 &&
        NdnRtcUtils::millisecondTimestamp() - recoveryCheckpointTimestamp_ > MaxInterruptionDelay)
    {
        rebufferingNum_++;
        
        resetData();
        consumer_->reset();
        
//        chaseEstimation_->reset();
        bufferEventsMask_ = ndnrtc::new_api::FrameBuffer::Event::StateChanged;
        isProcessing_ = true;
        
        if (rebufferingNum_ >= 5)
            exclusionFilter_ = -1;
        else
            exclusionFilter_ = deltaFrameSeqNo_+1;

        LogWarnC
        << "No data for the last " << MaxInterruptionDelay
        << " ms. Rebuffering " << rebufferingNum_
        << " exclusion " << exclusionFilter_
        << endl;
        LogStatC << "\tREBUFFERING\t" << rebufferingNum_ << endl;
        
    }
}