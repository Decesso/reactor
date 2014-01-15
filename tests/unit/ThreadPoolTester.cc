#include <thread/Pool.hh>

#include <cppunit/extensions/HelperMacros.h>

using namespace thread;

class ThreadPoolTester
: public CppUnit::TestFixture
, public util::Runnable
{
	CPPUNIT_TEST_SUITE(ThreadPoolTester);
	CPPUNIT_TEST(testConstruction);
	CPPUNIT_TEST_SUITE_END();

	volatile int count_;

public:
	void
	setUp()
	{
		count_ = 0;
	}

	virtual void
	run()
	{
		++count_;
	}

	void
	testConstruction()
	{
		{
			Pool tp(*this, 1);
		}
		CPPUNIT_ASSERT_EQUAL(1, (int)count_);
	}
};

CPPUNIT_TEST_SUITE_REGISTRATION(ThreadPoolTester);
