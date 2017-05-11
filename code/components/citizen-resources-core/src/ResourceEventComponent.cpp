﻿/*
 * This file is part of the CitizenFX project - http://citizen.re/
 *
 * See LICENSE and MENTIONS in the root of the source tree for information
 * regarding licensing.
 */

#include "StdInc.h"
#include "ResourceEventComponent.h"

#include <Resource.h>
#include <ResourceManager.h>

#include <msgpack.hpp>

static inline bool IsServer()
{
#ifdef IS_FXSERVER
	return true;
#else
	return false;
#endif
}

namespace fx
{
ResourceEventComponent::ResourceEventComponent()
{

}

void ResourceEventComponent::AttachToObject(Resource* object)
{
	m_resource = object;

	m_managerComponent = m_resource->GetManager()->GetComponent<ResourceEventManagerComponent>().GetRef();

	// start/stop handling events
	object->OnStart.Connect([=] ()
	{
		// pack the resource name
		msgpack::sbuffer buf;
		msgpack::packer<msgpack::sbuffer> packer(buf);

		// array of a single string
		packer.pack_array(1);
		packer.pack(m_resource->GetName());

		// send the event out to the world
		std::string event(buf.data(), buf.size());

		m_managerComponent->QueueEvent(fmt::sprintf("on%sResourceStart", IsServer() ? "Server" : "Client"), event);
		m_managerComponent->QueueEvent("onResourceStart", event);
	});

	object->OnStop.Connect([=] ()
	{
		// pack the resource name
		msgpack::sbuffer buf;
		msgpack::packer<msgpack::sbuffer> packer(buf);

		// array of a single string
		packer.pack_array(1);
		packer.pack(m_resource->GetName());

		// send the event out to the world
		std::string event(buf.data(), buf.size());

		m_managerComponent->QueueEvent(fmt::sprintf("on%sResourceStop", IsServer() ? "Server" : "Client"), event);
		m_managerComponent->QueueEvent("onResourceStop", event);
	});

	object->OnTick.Connect([=] ()
	{
		// take queued events and trigger them
		while (!m_eventQueue.empty())
		{
			// get the event
			EventData event;

			if (m_eventQueue.try_pop(event))
			{
				// and trigger it
				bool canceled = false;

				HandleTriggerEvent(event.eventName, event.eventPayload, event.eventSource, &canceled);
			}
		}
	});
}

void ResourceEventComponent::HandleTriggerEvent(const std::string& eventName, const std::string& eventPayload, const std::string& eventSource, bool* eventCanceled)
{
	OnTriggerEvent(eventName, eventPayload, eventSource, eventCanceled);
}

void ResourceEventComponent::QueueEvent(const std::string& eventName, const std::string& eventPayload, const std::string& eventSource /* = std::string() */)
{
	EventData event;
	event.eventName = eventName;
	event.eventPayload = eventPayload;
	event.eventSource = eventSource;

	{
		m_eventQueue.push(event);
	}
}

ResourceEventManagerComponent::ResourceEventManagerComponent()
	: m_wasLastEventCanceled(false)
{

}

void ResourceEventManagerComponent::Tick()
{
	// take queued events and trigger them
	while (!m_eventQueue.empty())
	{
		// get the event
		EventData event;
		
		if (m_eventQueue.try_pop(event))
		{
			// and trigger it
			TriggerEvent(event.eventName, event.eventPayload, event.eventSource);
		}
	}
}

bool ResourceEventManagerComponent::TriggerEvent(const std::string& eventName, const std::string& eventPayload, const std::string& eventSource /* = std::string() */)
{
	// add a value to signify event cancelation
	bool eventCanceled = false;

	m_eventCancelationStack.push(&eventCanceled);

	// trigger global handlers for the event
	OnTriggerEvent(eventName, eventPayload, eventSource, &eventCanceled);

	// trigger local handlers
	m_manager->ForAllResources([&] (fwRefContainer<Resource> resource)
	{
		// get the event component
		fwRefContainer<ResourceEventComponent> eventComponent = resource->GetComponent<ResourceEventComponent>();

		// if there's none, return
		if (!eventComponent.GetRef())
		{
			trace("no event component for resource %s\n", resource->GetName().c_str());
			return;
		}

		// continue on
		eventComponent->HandleTriggerEvent(eventName, eventPayload, eventSource, &eventCanceled);
	});

	// pop the stack entry
	m_eventCancelationStack.pop();

	// set state
	m_wasLastEventCanceled = eventCanceled;

	// return whether it was *not* canceled
	return !eventCanceled;
}

void ResourceEventManagerComponent::QueueEvent(const std::string& eventName, const std::string& eventPayload, const std::string& eventSource /* = std::string() */)
{
	EventData event;
	event.eventName = eventName;
	event.eventPayload = eventPayload;
	event.eventSource = eventSource;

	trace("queue event %s\n", eventName.c_str());

	{
		m_eventQueue.push(event);
	}

	// trigger global handlers for the queued event
	OnQueueEvent(eventName, eventPayload, eventSource);
}

void ResourceEventManagerComponent::AttachToObject(ResourceManager* object)
{
	m_manager = object;

	m_manager->OnTick.Connect([=] ()
	{
		this->Tick();
	});
}

static InitFunction initFunction([] ()
{
	Resource::OnInitializeInstance.Connect([] (Resource* resource)
	{
		resource->SetComponent<ResourceEventComponent>(new ResourceEventComponent());
	});

	ResourceManager::OnInitializeInstance.Connect([] (ResourceManager* manager)
	{
		manager->SetComponent<ResourceEventManagerComponent>(new ResourceEventManagerComponent());
	});
});
}