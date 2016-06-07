// 
// test-segment-controller.cc
//
//  Created by Peter Gusev on 04 June 2016.
//  Copyright 2013-2016 Regents of the University of California
//

#include <stdlib.h>

#include <boost/asio.hpp>
#include <boost/thread.hpp>
#include <boost/chrono.hpp>

#include "gtest/gtest.h"
#include "segment-controller.h"
#include "frame-data.h"

#include "mock-objects/segment-controller-observer-mock.h"

using namespace ::testing;
using namespace ndnrtc;
using namespace ndn;

// #define ENABLE_LOGGING

TEST(TestSegmentController, TestOnData)
{
	boost::asio::io_service io;
	SegmentController controller(io, 500);

#ifdef ENABLE_LOGGING
	ndnlog::new_api::Logger::initAsyncLogging();
	ndnlog::new_api::Logger::getLogger("").setLogLevel(ndnlog::NdnLoggerDetailLevelAll);
	controller.setLogger(&ndnlog::new_api::Logger::getLogger(""));
#endif

	OnData onData = controller.getOnDataCallback();
	OnTimeout onTimeout = controller.getOnTimeoutCallback();

	std::string segmentPrefix = "/ndn/edu/ucla/remap/peter/ndncon/instance1/ndnrtc/%FD%02/video/camera/hi/d/%FE%07/%00%00";
	boost::shared_ptr<Interest> i = boost::make_shared<Interest>(Name(segmentPrefix));
	boost::shared_ptr<Data> d = boost::make_shared<Data>(Name(segmentPrefix));

	MockSegmentControllerObserver o;
	controller.attach(&o);

	boost::function<void(const boost::shared_ptr<WireSegment>&)> checkSegment = [i,d]
		(const boost::shared_ptr<WireSegment>& seg)
		{
			EXPECT_EQ(d, seg->getData());
			EXPECT_EQ(i, seg->getInterest());
		};

	EXPECT_CALL(o, segmentArrived(_))
		.Times(1)
		.WillOnce(Invoke(checkSegment));

	onData(i, d);
	controller.detach(&o);

	EXPECT_CALL(o, segmentArrived(_))
		.Times(0);
	onData(i, d);
}

TEST(TestSegmentController, TestOnBadNamedData)
{
	boost::asio::io_service io;
	SegmentController controller(io, 500);

#ifdef ENABLE_LOGGING
	ndnlog::new_api::Logger::initAsyncLogging();
	ndnlog::new_api::Logger::getLogger("").setLogLevel(ndnlog::NdnLoggerDetailLevelDebug);
	controller.setLogger(&ndnlog::new_api::Logger::getLogger(""));
#endif

	OnData onData = controller.getOnDataCallback();
	OnTimeout onTimeout = controller.getOnTimeoutCallback();

	std::string segmentPrefix = "/ndn/edu/ucla/remap/peter/ndncon/instance1/ndnrtc/%FD%02/video/camera/hi/%FE%07/%00%00";
	boost::shared_ptr<Interest> i = boost::make_shared<Interest>(Name(segmentPrefix));
	boost::shared_ptr<Data> d = boost::make_shared<Data>(Name(segmentPrefix));

	MockSegmentControllerObserver o;
	controller.attach(&o);

	EXPECT_CALL(o, segmentArrived(_))
		.Times(0);

	onData(i, d);
	controller.detach(&o);
}

TEST(TestSegmentController, TestOnTimeout)
{
	boost::asio::io_service io;
	SegmentController controller(io, 500);

#ifdef ENABLE_LOGGING
	ndnlog::new_api::Logger::initAsyncLogging();
	ndnlog::new_api::Logger::getLogger("").setLogLevel(ndnlog::NdnLoggerDetailLevelDebug);
	controller.setLogger(&ndnlog::new_api::Logger::getLogger(""));
#endif

	OnData onData = controller.getOnDataCallback();
	OnTimeout onTimeout = controller.getOnTimeoutCallback();

	std::string segmentPrefix = "/ndn/edu/ucla/remap/peter/ndncon/instance1/ndnrtc/%FD%02/video/camera/hi/d/%FE%07/%00%00";
	boost::shared_ptr<Interest> i = boost::make_shared<Interest>(Name(segmentPrefix));
	boost::shared_ptr<Data> d = boost::make_shared<Data>(Name(segmentPrefix));

	MockSegmentControllerObserver o;
	controller.attach(&o);

	boost::function<void(const NamespaceInfo&)> checkTimeout = [i]
		(const NamespaceInfo& info)
		{
			EXPECT_EQ(info.getPrefix(), i->getName());
		};

	EXPECT_CALL(o, segmentRequestTimeout(_))
		.Times(1)
		.WillOnce(Invoke(checkTimeout));

	onTimeout(i);
	controller.detach(&o);
}

TEST(TestSegmentController, TestStarvation)
{
	boost::asio::io_service io;
	boost::shared_ptr<boost::asio::io_service::work> work(new boost::asio::io_service::work(io));
	boost::thread t([&io](){
		io.run();
	});

	MockSegmentControllerObserver o;
	EXPECT_CALL(o, segmentStarvation())
		.Times(1);

	{
		SegmentController controller(io, 500);

#ifdef ENABLE_LOGGING
		ndnlog::new_api::Logger::initAsyncLogging();
		ndnlog::new_api::Logger::getLogger("").setLogLevel(ndnlog::NdnLoggerDetailLevelDebug);
		controller.setLogger(&ndnlog::new_api::Logger::getLogger(""));
#endif

		controller.attach(&o);
		boost::this_thread::sleep_for(boost::chrono::milliseconds(1050));
	}
	work.reset();
	t.join();
}

//******************************************************************************
int main(int argc, char **argv) {
	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}