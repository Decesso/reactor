#include <netdb.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <poll.h>
#include <string>
#include <stdexcept>
#include <vector>
#include <cerrno>
#include <map>
#include <sstream>
#include <sys/time.h> // gettimeofday()
#include <queue>
#include <set>

#define ARRAY_LENGTH(x) (sizeof(x) / sizeof(x[0]))

class ErrnoException : std::runtime_error {
	static std::string construct(const std::string &name);

public:
	explicit ErrnoException(const std::string &name);
};

std::string
ErrnoException::construct(const std::string &name)
{
	std::ostringstream oss;
	char buf[512];

	strerror_r(errno, buf, sizeof(buf));
	oss << name << ": " << buf;
	return oss.str();
}

ErrnoException::ErrnoException(const std::string &name)
: std::runtime_error(construct(name))
{}

class Noncopyable {
	Noncopyable(const Noncopyable &);
	Noncopyable &operator=(const Noncopyable &);

public:
	Noncopyable(void) {}
};

class Fd : public Noncopyable {
	int fd_;

public:
	enum { INVALID = -1 };

	static const Fd STDIN;
	static const Fd STDOUT;
	static const Fd STDERR;

	explicit Fd(int fd = INVALID) : fd_(fd) {}
	~Fd() { release(); }

	bool valid() const { return fd_ != INVALID; }

	int get() const { return fd_; }
	int release();
	void reset(int fd = INVALID) { release(); fd_ = fd; }

	size_t read(void *buffer, size_t size) const;
	size_t write(const void *buffer, size_t length) const;
};

const Fd Fd::STDIN(STDIN_FILENO);
const Fd Fd::STDOUT(STDOUT_FILENO);
const Fd Fd::STDERR(STDERR_FILENO);

int
Fd::release(void)
{
	int fd = fd_;

	if (valid()) {
		close(fd_);
		fd_ = INVALID;
	}

	return fd;
}

size_t
Fd::read(void *buffer, size_t size)
const
{
	ssize_t ret = ::read(get(), buffer, size);

	if (ret < 0) throw ErrnoException("read");
	return (size_t)ret;
}

size_t
Fd::write(const void *buffer, size_t length)
const
{
	ssize_t ret = ::write(get(), buffer, length);

	if (ret < 0) throw ErrnoException("write");
	return (size_t)ret;
}

class Specifier {
	std::string spec_;
	int aiFlags_;

public:
	Specifier(const std::string &spec = "", int aiFlags = 0)
	: spec_(spec)
	, aiFlags_(aiFlags)
	{}

	const std::string &spec() const { return spec_; }
	int aiFlags() const { return aiFlags_; }
};

class Host : public Specifier {
public:
	static const int NUMERIC;

	Host(const std::string &spec = "", int aiFlags = 0)
	: Specifier(spec, aiFlags)
	{}
};

const int Host::NUMERIC = AI_NUMERICHOST;

class Ip : public Host {
public:
	static const Ip ANY;

	explicit Ip(const std::string &ip = "", int aiFlags = 0)
	: Host(ip, aiFlags | NUMERIC)
	{}
};

const Ip Ip::ANY("");

class Service : public Specifier {
public:
	static const int NUMERIC;

	explicit Service(const std::string &spec = "", int aiFlags = 0)
	: Specifier(spec, aiFlags)
	{}
};

const int Service::NUMERIC = AI_NUMERICSERV;

class Port : public Service {
public:
	Port(const std::string &port = "", int aiFlags = 0)
	: Service(port, aiFlags | NUMERIC)
	{}
};

class Socket {
	Fd fd_;
	int type_;

public:
	static const int ANY;
	static const int STREAM;
	static const int DGRAM;

	Socket(int type) : type_(type) {}

	void connect(const Host &targetHost, const Service &targetServ);
	const Fd &fd() const { return fd_; }
};

const int Socket::ANY = 0;
const int Socket::STREAM = SOCK_STREAM;
const int Socket::DGRAM = SOCK_DGRAM;

void
Socket::connect(const Host &targetHost, const Service &targetServ)
{
	int ret;
	struct addrinfo hints, *res;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = type_;
	hints.ai_flags = targetHost.aiFlags() | targetServ.aiFlags();

	ret = getaddrinfo(targetHost.spec().c_str(), targetServ.spec().c_str(), &hints, &res);
	if (ret) {
		throw std::runtime_error(gai_strerror(ret));
	}

	for (struct addrinfo *p = res; p; p = p->ai_next) {
		fd_.reset(socket(p->ai_family, p->ai_socktype, p->ai_protocol));
		if (!fd_.valid()) continue;

		ret = ::connect(fd_.get(), p->ai_addr, p->ai_addrlen);
		if (ret != -1) break;

		fd_.release();
	}

	freeaddrinfo(res);

	if (!fd_.valid()) {
		throw std::runtime_error("failed to connect");
	}
}

class StreamSock : public Socket {
public:
	StreamSock() : Socket(STREAM) {}
};

class DgramSock : public Socket {
public:
	DgramSock() : Socket(DGRAM) {}
};

class Action {
public:
	virtual ~Action() {}
	virtual Action *clone() const = 0;

	virtual void perform() = 0;
};

template <class T>
class ActionMethod : public Action {
public:
	typedef void (T::*Method)();

private:
	T &object_;
	Method method_;

public:
	ActionMethod(T &object, Method method)
	: object_(object)
	, method_(method)
	{}

	virtual Action *clone() const { return new ActionMethod<T>(*this); }

	virtual void perform() { (object_.*method_)(); }
};

template <class T>
ActionMethod<T>
genActionMethod(T &object, void (T::*method)())
{
	return ActionMethod<T>(object, method);
}

class DiffTime {
	int64_t raw_;

	explicit DiffTime(int64_t raw) : raw_(raw) {}

public:
	static DiffTime raw(int64_t raw) { return DiffTime(raw); }
	static DiffTime ms(int32_t ms);

	int64_t raw() const { return raw_; }
	int32_t ms() const;
	bool positive() const { return raw_ > 0; }
};

DiffTime
DiffTime::ms(int32_t ms)
{
	int64_t i = (int64_t)(ms / 1000) << 32;
	int64_t frac = ((int64_t)(ms % 1000) << 32) / 1000;
	return DiffTime(i | frac);
}

int32_t
DiffTime::ms() const
{
	int64_t i = raw_ >> 32;
	int64_t frac = ((raw_ & 0xffffffff) * 1000) >> 32;
	return i * 1000 + frac;
}

class Time {
	uint64_t time_;

	explicit Time(uint64_t time) : time_(time) {}

public:
	static Time now();

	Time() : time_(0) {}

	DiffTime operator-(const Time &rhs) const { return DiffTime::raw(time_ - rhs.time_); }

	Time operator+(const DiffTime &rhs) const { return Time(time_ + rhs.raw()); }
	Time &operator+=(const DiffTime &rhs) { time_ += rhs.raw(); return *this; }
	Time operator-(const DiffTime &rhs) const { return Time(time_ - rhs.raw()); }
	Time &operator-=(const DiffTime &rhs) { time_ -= rhs.raw(); return *this; }

	bool operator<(const Time &rhs) const { return time_ < rhs.time_; }

	time_t unixtime() const { return time_ >> 32; }
	uint32_t fraction(uint32_t multiplier) const { return ((time_ & 0xffffffff) * multiplier) >> 32; }
	uint32_t msFraction() const { return fraction(1000); }
	uint32_t usFraction() const { return fraction(1000 * 1000); }
	uint32_t nsFraction() const { return fraction(1000 * 1000 * 1000); }
};

Time
Time::now()
{
	struct timeval tv;
	int ret = gettimeofday(&tv, 0);
	if (ret) throw ErrnoException("gettimeofday");
	return Time(((uint64_t)tv.tv_sec << 32) | (((uint64_t)tv.tv_usec << 32) / 1000000));
}

class Timer {
	DiffTime interval_;
	Time expiration_;
	bool oneShot_;

public:
	Timer(const DiffTime &interval, bool oneShot = false)
	: interval_(interval)
	, expiration_(Time::now() + interval)
	, oneShot_(oneShot)
	{}

	bool operator<(const Timer &rhs) const { return expiration_ < rhs.expiration_; }
	void operator++() { expiration_ += interval_; }

	const Time &expiration() const { return expiration_; }
};

class ActionsGuard {
	typedef std::set<Action *> Actions;

	Actions actions_;

public:
	~ActionsGuard() { clear(); }

	Action *reproduce(const Action &action);
	void forget(const Action &action);
	void clear();
};

void
ActionsGuard::clear()
{
	for (Actions::iterator i(actions_.begin()); i != actions_.end(); ++i) {
		delete *i;
	}
	actions_.clear();
}

Action *
ActionsGuard::reproduce(const Action &action)
{
	Action *a = action.clone();
	actions_.insert(a);
	return a;
}

class Poller : public Noncopyable {
	typedef std::map<int, Action *> FdHandlers;
	typedef std::pair<Timer, Action *> TimerAction;
	class TimerActionComparator : public std::less<TimerAction> {
	public:
		bool operator() (const TimerAction &a, const TimerAction &b) const { return a.first < b.first; }
	};
	typedef std::priority_queue<TimerAction, std::vector<TimerAction>, TimerActionComparator> Timers;

	ActionsGuard guard_;
	std::vector<struct pollfd> fds_;
	FdHandlers fdHandlers_;
	bool quit_;
	Timers timers_;

public:
	Poller() : quit_(false) {}

	int run();
	void quit() { quit_ = true; }
	void add(const Fd &fd);

	void add(const Fd &fd, const Action &action);
	void add(const Timer &timer, const Action &action);
};

void
Poller::add(const Fd &fd, const Action &action)
{
	Action *a = guard_.reproduce(action);
	add(fd);
	fdHandlers_.insert(std::make_pair(fd.get(), a));
}

void
Poller::add(const Timer &timer, const Action &action)
{
	Action *a = guard_.reproduce(action);
	timers_.push(std::make_pair(timer, a));
}

int
Poller::run(void)
{
	while (!quit_) {
		int ret;
		int ms = -1;

		while (!timers_.empty()) {
			TimerAction ta(timers_.top());
			const DiffTime dt(ta.first.expiration() - Time::now());

			if (!dt.positive()) {
				timers_.pop();
				ta.second->perform();
				++ta.first;
				timers_.push(ta);
			} else {
				ms = dt.ms();
				break;
			}
		}
		ret = poll(&fds_[0], fds_.size(), ms);
		if (ret < 0) {
			throw ErrnoException("poll");
		} else if (ret > 0) {
			for (size_t i = 0; i < fds_.size(); ++i) {
				if (fds_[i].revents) {
					FdHandlers::iterator j(fdHandlers_.find(fds_[i].fd));
					if (j != fdHandlers_.end()) {
						j->second->perform();
					}
				}
			}
		}
	}
	return EXIT_SUCCESS;
}

void
Poller::add(const Fd &fd)
{
	if (fd.valid()) {
		struct pollfd pfd;

		pfd.fd = fd.get();
		pfd.events = POLLIN;
		pfd.revents = 0;

		fds_.push_back(pfd);
	}
}

class Client : public Noncopyable {
	Poller &poller_;
	Host targetHost_;
	Service targetServ_;
	Host sourceHost_;
	StreamSock sock_;

public:
	Client(Poller &poller)
	: poller_(poller)
	{}

	void setTarget(const Host &targetHost, const Service &targetServ);
	void connect();

	const Fd &fd() const { return sock_.fd(); }
};

void
Client::setTarget(const Host &targetHost, const Service &targetServ)
{
	targetHost_ = targetHost;
	targetServ_ = targetServ;
}

void
Client::connect()
{
	sock_.connect(targetHost_, targetServ_);
}

class Control {
	Poller poller_;
	Client client_;

public:
	Control(int argc, char *argv[]);

	void onFdStdin();
	void onFdSock();
	void onTimer();
	int run() { return poller_.run(); }
};

Control::Control(int argc, char *argv[])
: client_(poller_)
{
	if (argc != 3) throw std::runtime_error("argc must be 3");
//	client_.setTarget(Ip("127.0.0.1"), Port("8080"));
	client_.setTarget(Host(argv[1]), Service(argv[2]));
	client_.connect();

	poller_.add(Fd::STDIN, genActionMethod(*this, &Control::onFdStdin));
	poller_.add(client_.fd(), genActionMethod(*this, &Control::onFdSock));
	poller_.add(Timer(DiffTime::ms(1000)), genActionMethod(*this, &Control::onTimer));
}

void
Control::onFdStdin()
{
	char buf[128];
	size_t rd = Fd::STDIN.read(buf, sizeof(buf));

	if (!rd) {
		poller_.quit();
	} else {
		size_t wr = client_.fd().write(buf, rd);

		if (wr != rd) {
			throw std::runtime_error("partial send");
		}
	}
}

void
Control::onFdSock()
{
	char buf[128];
	size_t rd = client_.fd().read(buf, sizeof(buf));

	if (!rd) {
		poller_.quit();
	} else {
		size_t wr = Fd::STDOUT.write(buf, rd);

		if (wr != rd) {
			throw std::runtime_error("partial send");
		}
	}
}

void
Control::onTimer()
{
	fprintf(stderr, "timer\n");
}

int
main(int argc, char *argv[])
{
	return Control(argc, argv).run();
}
