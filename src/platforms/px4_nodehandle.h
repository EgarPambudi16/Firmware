/****************************************************************************
 *
 *   Copyright (c) 2014 PX4 Development Team. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name PX4 nor the names of its contributors may be
 *    used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 ****************************************************************************/

/**
 * @file px4_nodehandle.h
 *
 * PX4 Middleware Wrapper Node Handle
 */
#pragma once

/* includes for all platforms */
#include "px4_subscriber.h"
#include "px4_publisher.h"
#include "px4_middleware.h"

#if defined(__linux) || (defined(__APPLE__) && defined(__MACH__))
/* includes when building for ros */
#include "ros/ros.h"
#include <list>
#include <inttypes.h>
#else
/* includes when building for NuttX */
#include <poll.h>
#endif

namespace px4
{
#if defined(__linux) || (defined(__APPLE__) && defined(__MACH__))
class NodeHandle :
	private ros::NodeHandle
{
public:
	NodeHandle() :
		ros::NodeHandle(),
		_subs(),
		_pubs()
	{}

	~NodeHandle()
	{
		//XXX empty lists
	};

	/**
	 * Subscribe with callback to function
	 * @param topic		Name of the topic
	 * @param fb		Callback, executed on receiving a new message
	 */
	template<typename M>
	Subscriber *subscribe(const char *topic, void(*fp)(const M&))
	{
		Subscriber *sub = new SubscriberROS<M>(std::bind(fp, std::placeholders::_1));
		ros::Subscriber ros_sub = ros::NodeHandle::subscribe(topic, kQueueSizeDefault, &SubscriberROS<M>::callback, (SubscriberROS<M>*)sub);
		((SubscriberROS<M>*)sub)->set_ros_sub(ros_sub);
		_subs.push_back(sub);
		return sub;
	}

	/**
	 * Subscribe with callback to class method
	 * @param topic		Name of the topic
	 * @param fb		Callback, executed on receiving a new message
	 */
	template<typename M, typename T>
	Subscriber *subscribe(const char *topic, void(T::*fp)(const M&), T *obj)
	{
		Subscriber *sub = new SubscriberROS<M>(std::bind(fp, obj, std::placeholders::_1));
		ros::Subscriber ros_sub = ros::NodeHandle::subscribe(topic, kQueueSizeDefault, &SubscriberROS<M>::callback, (SubscriberROS<M>*)sub);
		((SubscriberROS<M>*)sub)->set_ros_sub(ros_sub);
		_subs.push_back(sub);
		return sub;
	}

	/**
	 * Subscribe with no callback, just the latest value is stored on updates
	 * @param topic		Name of the topic
	 */
	template<typename M>
	Subscriber *subscribe(const char *topic)
	{
		Subscriber *sub = new SubscriberROS<M>();
		ros::Subscriber ros_sub = ros::NodeHandle::subscribe(topic, kQueueSizeDefault, &SubscriberROS<M>::callback, (SubscriberROS<M>*)sub);
		((SubscriberROS<M>*)sub)->set_ros_sub(ros_sub);
		_subs.push_back(sub);
		return sub;
	}

	/**
	 * Advertise topic
	 * @param topic		Name of the topic
	 */
	template<typename M>
	Publisher *advertise(const char *topic)
	{
		ros::Publisher ros_pub = ros::NodeHandle::advertise<M>(topic, kQueueSizeDefault);
		Publisher *pub =  new Publisher(ros_pub);
		_pubs.push_back(pub);
		return pub;
	}

	/**
	 * Calls all callback waiting to be called
	 */
	void spinOnce() { ros::spinOnce(); }

	/**
	 * Keeps calling callbacks for incomming messages, returns when module is terminated
	 */
	void spin() { ros::spin(); }


private:
	static const uint32_t kQueueSizeDefault = 1000;		/**< Size of queue for ROS */
	std::list<Subscriber *> _subs;				/**< Subcriptions of node */
	std::list<Publisher *> _pubs;				/**< Publications of node */
};
#else
class __EXPORT NodeHandle
{
public:
	NodeHandle() :
		_subs(),
		_pubs(),
		_sub_min_interval(nullptr)
	{}

	~NodeHandle() {};

	/**
	 * Subscribe with callback to function
	 * @param meta		Describes the topic which nodehande should subscribe to
	 * @param callback	Callback, executed on receiving a new message
	 * @param interval	Minimal interval between calls to callback
	 */

	template<typename M>
	Subscriber *subscribe(const struct orb_metadata *meta,
			      std::function<void(const M &)> callback,
			      unsigned interval)
	{
		SubscriberUORB<M> *sub_px4 = new SubscriberUORB<M>(meta, interval, callback, &_subs);

		/* Check if this is the smallest interval so far and update _sub_min_interval */
		if (_sub_min_interval == nullptr || _sub_min_interval->getInterval() > sub_px4->getInterval()) {
			_sub_min_interval = sub_px4;
		}

		return (Subscriber *)sub_px4;
	}

	/**
	 * Advertise topic
	 * @param meta		Describes the topic which is advertised
	 */
	template<typename M>
	Publisher *advertise(const struct orb_metadata *meta)
	{
		//XXX
		Publisher *pub = new Publisher(meta, &_pubs);
		return pub;
	}

	/**
	 * Calls all callback waiting to be called
	 */
	void spinOnce()
	{
		/* Loop through subscriptions, call callback for updated subscriptions */
		uORB::SubscriptionNode *sub = _subs.getHead();
		int count = 0;

		while (sub != nullptr) {
			if (count++ > kMaxSubscriptions) {
				PX4_WARN("exceeded max subscriptions");
				break;
			}

			sub->update();
			sub = sub->getSibling();
		}
	}

	/**
	 * Keeps calling callbacks for incomming messages, returns when module is terminated
	 */
	void spin()
	{
		while (ok()) {
			const int timeout_ms = 100;

			/* Only continue in the loop if the nodehandle has subscriptions */
			if (_sub_min_interval == nullptr) {
				usleep(timeout_ms * 1000);
				continue;
			}

			/* Poll fd with smallest interval */
			struct pollfd pfd;
			pfd.fd = _sub_min_interval->getHandle();
			pfd.events = POLLIN;

			if (poll(&pfd, 1, timeout_ms) <= 0) {
				/* timed out */
				continue;
			}

			spinOnce();
		}
	}
private:
	static const uint16_t kMaxSubscriptions = 100;
	List<uORB::SubscriptionNode *> _subs;		/**< Subcriptions of node */
	List<uORB::PublicationNode *> _pubs;		/**< Publications of node */
	uORB::SubscriptionNode *_sub_min_interval;	/**< Points to the sub wtih the smallest interval
							  of all Subscriptions in _subs*/
};
#endif
}
