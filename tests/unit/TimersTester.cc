#include <reactor/Timers.hh>

#include <tests/unit/mock/Mocked.hh>
#include <tests/unit/mock/MockRegistry.hh>

#include <cppunit/extensions/HelperMacros.h>

using namespace util;
using namespace reactor;

class TimersTester : public CppUnit::TestFixture {
	CPPUNIT_TEST_SUITE(TimersTester);
	CPPUNIT_TEST(testAddTimerActions);
	CPPUNIT_TEST(testFireAllExpired);
	CPPUNIT_TEST_SUITE_END();

	const MethodCommand1<void, TimersTester, const TimerEvent &> methodCommand1_;
	const MethodCommand1<void, TimersTester, const TimerEvent &> methodCommand2_;

	Backlog *bl_;
	Timers *t_;

	size_t command1Count_;
	size_t command2Count_;

public:
	TimersTester()
	: methodCommand1_(commandForMethod(*this, &TimersTester::command1))
	, methodCommand2_(commandForMethod(*this, &TimersTester::command2))
	{}

	void
	setUp()
	{
		bl_ = new Backlog();
		t_ = new Timers(*bl_, &TimersTester::now);
		command1Count_ = command2Count_ = 0;
	}

	void
	tearDown()
	{
		delete t_;
		delete bl_;
	}

	void
	command1(const TimerEvent &)
	{
		++command1Count_;
	}

	void
	command2(const TimerEvent &)
	{
		++command2Count_;
	}

	void
	testAddTimerActions()
	{
		t_->add(Timer(DiffTime::raw(2), 0, Time::raw(0)), methodCommand1_);
		t_->add(Timer(DiffTime::raw(3), 0, Time::raw(0)), methodCommand2_);
	}

	static Time
	now()
	{
		Mocked &m = MockRegistry::find("now");
		return Time::raw(m.expectedInt());
	}

	void
	executeAll()
	{
		while (!bl_->empty()) {
			Backlog::Job *job = bl_->dequeue();
			job->execute();
			delete job;
		}
	}

	void
	testFireAllExpired()
	{
		Mocked now("now");

		t_->add(Timer(DiffTime::raw(2), 2, Time::raw(0)), methodCommand1_);
		t_->add(Timer(DiffTime::raw(3), 1, Time::raw(0)), methodCommand2_);

		command1Count_ = command2Count_ = 0;
		now.expect(0);
		t_->harvest();
		CPPUNIT_ASSERT_EQUAL(true, t_->isTicking());
		executeAll();
		CPPUNIT_ASSERT_EQUAL((size_t)0, command1Count_);
		CPPUNIT_ASSERT_EQUAL((size_t)0, command2Count_);
		now.expect(0);
		CPPUNIT_ASSERT_EQUAL((int64_t)2, t_->remainingTime().raw());

		command1Count_ = command2Count_ = 0;
		now.expect(2);
		now.expect(2);
		t_->harvest();
		CPPUNIT_ASSERT_EQUAL(true, t_->isTicking());
		executeAll();
		CPPUNIT_ASSERT_EQUAL((size_t)1, command1Count_);
		CPPUNIT_ASSERT_EQUAL((size_t)0, command2Count_);
		now.expect(2);
		CPPUNIT_ASSERT_EQUAL((int64_t)1, t_->remainingTime().raw());

		command1Count_ = command2Count_ = 0;
		now.expect(3);
		now.expect(3);
		t_->harvest();
		CPPUNIT_ASSERT_EQUAL(true, t_->isTicking());
		executeAll();
		CPPUNIT_ASSERT_EQUAL((size_t)0, command1Count_);
		CPPUNIT_ASSERT_EQUAL((size_t)1, command2Count_);
		now.expect(3);
		CPPUNIT_ASSERT_EQUAL((int64_t)1, t_->remainingTime().raw());

		command1Count_ = command2Count_ = 0;
		now.expect(4);
		t_->harvest();
		CPPUNIT_ASSERT_EQUAL(false, t_->isTicking());
		executeAll();
		CPPUNIT_ASSERT_EQUAL((size_t)1, command1Count_);
		CPPUNIT_ASSERT_EQUAL((size_t)0, command2Count_);
	}
};

CPPUNIT_TEST_SUITE_REGISTRATION(TimersTester);
