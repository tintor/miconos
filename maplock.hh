#pragma once

#include <mutex>
#include <condition_variable>

template<typename T>
struct AutoMapLock;

template<typename T>
class MapLock
{
	struct Bucket
	{
		std::mutex mutex;
		std::condition_variable cond;
		AutoMapLock<T>* head;
		Bucket() : head(nullptr) { }
	};
	Bucket& get_bucket(T key) { return m_bucket[std::hash<T>()(key) % 64]; }
	Bucket m_bucket[64];
	friend class AutoMapLock<T>;
};

template<typename T>
struct AutoMapLock
{
	AutoMapLock(T key, MapLock<T>& mgr) : m_key(key), m_bucket(mgr.get_bucket(key))
	{
		std::unique_lock<std::mutex> lock(m_bucket.mutex);
		while (is_locked())
		{
			m_bucket.cond.wait(lock);
		}
		m_next = m_bucket.head;
		m_bucket.head = this;
	}

	~AutoMapLock()
	{
		std::unique_lock<std::mutex> lock(m_bucket.mutex);
		AutoMapLock** ptr = &m_bucket.head;
		while (*ptr != this) ptr = &(*ptr)->m_next;
		*ptr = m_next;
		m_bucket.cond.notify_all();
	}

private:
	bool is_locked()
	{
		for (AutoMapLock* i = m_bucket.head; i; i = i->m_next)
		{
			if (i->m_key == m_key) return true;
		}
		return false;
	}

private:
	T m_key;
	AutoMapLock* m_next;
	typename MapLock<T>::Bucket& m_bucket;
};
