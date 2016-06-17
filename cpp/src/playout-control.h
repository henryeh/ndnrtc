// 
// playout-control.h
//
//  Created by Peter Gusev on 16 June 2016.
//  Copyright 2013-2016 Regents of the University of California
//

#ifndef __playout_control_h__
#define __playout_control_h__

#include "ndnrtc-object.h"
#include "frame-buffer.h"
#include "playout.h"

namespace ndnrtc {
	class IPlayout;
	class IPlaybackQueue;

	class IPlayoutControl : public IPlaybackQueueObserver,
							public IPlayoutObserver
	{
	public:
		virtual void allowPlayout(bool allow) = 0;
		virtual void onNewSampleReady() = 0;
		virtual void onQueueEmpty() = 0;
	};

	/**
	 * PlayoutControl implements functionality to start/stop samples playout.
	 * Playout starts whenever it is allowed AND playback queue size reached 
	 * or excedes minimal playable level (target size). It will fast forward
	 * the excess upon start.
	 * Playout stops if is is not allowed.
	 */
	class PlayoutControl : 	public NdnRtcComponent,
							public IPlayoutControl
	{
	public:
		PlayoutControl(const boost::shared_ptr<IPlayout>& playout, 
			const boost::shared_ptr<IPlaybackQueue> queue,
			unsigned int minimalPlayableLevel);

		void allowPlayout(bool allow);
		void onNewSampleReady();
		void onQueueEmpty() { /*ignored*/ }

	private:
		bool playoutAllowed_;
		boost::shared_ptr<IPlayout> playout_;
		boost::shared_ptr<IPlaybackQueue> queue_;
		unsigned int thresholdMs_;

		void checkPlayout();
	};
}

#endif