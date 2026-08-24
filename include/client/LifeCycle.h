#pragma once

#include <QSharedPointer>

namespace NetCore {

// 非 QObject 的弱引用追踪：self_ 空析构器自持，
// 对象析构时 self_ 随之析构，弱引用自动失效
class TrackLifeCycle {
public:
	TrackLifeCycle()
		: self_(QSharedPointer<TrackLifeCycle>(this, [](auto*) {}))
	{
	}

	TrackLifeCycle(const TrackLifeCycle&) = delete;
	TrackLifeCycle& operator=(const TrackLifeCycle&) = delete;

	QWeakPointer<TrackLifeCycle> AsWeakPtr() const
	{
		return self_.toWeakRef();
	}

	template <typename T>
	QWeakPointer<T> AsWeakPtrT() const
	{
		return qSharedPointerDynamicCast<T>(self_).toWeakRef();
	}

protected:
	virtual ~TrackLifeCycle() = default;

private:
	QSharedPointer<TrackLifeCycle> self_;
};

} // namespace NetCore
