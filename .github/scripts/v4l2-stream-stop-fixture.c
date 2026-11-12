/* SPDX-License-Identifier: GPL-2.0-only */
/* Source-extracted media-core tests. Kernel services and pad callbacks below
 * are explicit host test doubles, not hardware or complete V4L2/PM emulation.
 */
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef uint32_t u32;
typedef uint64_t u64;
#define BIT_ULL(n) (UINT64_C(1) << (n))
#define BITS_PER_BYTE 8
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define MEDIA_PAD_FL_SOURCE 1
@FLAGS@
#define V4L2_SUBDEV_ROUTE_FL_ACTIVE 1
#define ENOIOCTLCMD 515

struct fixture;
struct device { struct fixture *fixture; };
struct v4l2_subdev_stream_config { u32 pad, stream; bool enabled; };
struct v4l2_subdev_route { u32 source_pad, source_stream, flags; };
struct v4l2_subdev_state {
	pthread_mutex_t lock;
	int depth;
	struct { unsigned int num_configs; struct v4l2_subdev_stream_config *configs; } stream_configs;
	struct { unsigned int num_routes; struct v4l2_subdev_route *routes; } routing;
};
struct media_pad { u32 flags, index; };
struct media_device { struct device *dev; };
struct media_entity {
	const char *name;
	unsigned int num_pads;
	struct media_pad *pads;
	struct { struct media_device *mdev; } graph_obj;
};
struct v4l2_subdev;
struct v4l2_subdev_video_ops { int (*s_stream)(struct v4l2_subdev *, int); };
struct v4l2_subdev_pad_ops {
	int (*enable_streams)(struct v4l2_subdev *, struct v4l2_subdev_state *, u32, u64);
	int (*disable_streams)(struct v4l2_subdev *, struct v4l2_subdev_state *, u32, u64);
};
struct v4l2_subdev_ops {
	const struct v4l2_subdev_video_ops *video;
	const struct v4l2_subdev_pad_ops *pad;
};
struct v4l2_subdev {
	struct device *dev;
	struct media_entity entity;
	const struct v4l2_subdev_ops *ops;
	struct v4l2_subdev_state *active_state;
	u32 flags;
	u64 enabled_pads;
	bool s_stream_enabled;
};
struct fixture {
	struct device dev;
	struct v4l2_subdev sd;
	struct v4l2_subdev_state state;
	struct media_pad pad;
	struct media_device media;
	struct v4l2_subdev_stream_config configs[2];
	struct v4l2_subdev_route routes[2];
	int references, starts, stops, start_error, stop_error;
	bool controls_owned, privacy;
};
static unsigned int assertions;
#define CHECK(label, c) do { ++assertions; if (!(c)) { \
	fprintf(stderr, "ASSERT %s\n", label); return 1; } } while (0)

static void log_stub(struct device *dev, const char *fmt, ...) { (void)dev; (void)fmt; }
#define dev_warn(...) log_stub(__VA_ARGS__)
#define dev_dbg(...) log_stub(__VA_ARGS__)
#define WARN_ON(c) (!!(c))
#define v4l2_subdev_has_op(sd, k, op) ((sd)->ops->k && (sd)->ops->k->op)
extern const struct v4l2_subdev_ops v4l2_subdev_call_wrappers;
@DISPATCH@
#define media_entity_for_each_pad(e, p) \
	for ((p) = (e)->pads; (p) < (e)->pads + (e)->num_pads; ++(p))
#define for_each_active_route(r, p) \
	for ((p) = (r)->routes; (p) < (r)->routes + (r)->num_routes; ++(p)) \
		if ((p)->flags & V4L2_SUBDEV_ROUTE_FL_ACTIVE)

static int call_s_stream(struct v4l2_subdev *, int);
int v4l2_subdev_s_stream_helper(struct v4l2_subdev *, int);
int v4l2_subdev_enable_streams(struct v4l2_subdev *, u32, u64);
int v4l2_subdev_disable_streams(struct v4l2_subdev *, u32, u64);
bool v4l2_subdev_is_streaming(struct v4l2_subdev *);
static struct v4l2_subdev_state *v4l2_subdev_lock_and_get_active_state(struct v4l2_subdev *sd)
{
	struct v4l2_subdev_state *s = sd->active_state;
	assert(s && !pthread_mutex_lock(&s->lock));
	assert(!s->depth);
	++s->depth;
	return s;
}
static void v4l2_subdev_unlock_state(struct v4l2_subdev_state *s)
{
	assert(s->depth == 1);
	--s->depth;
	assert(!pthread_mutex_unlock(&s->lock));
}
static struct v4l2_subdev_state *v4l2_subdev_get_locked_active_state(struct v4l2_subdev *sd)
{
	assert(sd->active_state && sd->active_state->depth == 1);
	return sd->active_state;
}
static void v4l2_subdev_enable_privacy_led(struct v4l2_subdev *sd)
{ sd->dev->fixture->privacy = true; }
static void v4l2_subdev_disable_privacy_led(struct v4l2_subdev *sd)
{ sd->dev->fixture->privacy = false; }

@SOURCE@

static int fake_enable(struct v4l2_subdev *sd, struct v4l2_subdev_state *state,
		       u32 pad, u64 mask)
{
	struct fixture *f = sd->dev->fixture;
	(void)pad; (void)mask;
	assert(state->depth == 1);
	++f->starts;
	if (f->start_error)
		return f->start_error;
	++f->references;
	f->controls_owned = true;
	return 0;
}
static int fake_disable(struct v4l2_subdev *sd, struct v4l2_subdev_state *state,
			u32 pad, u64 mask)
{
	struct fixture *f = sd->dev->fixture;
	(void)pad; (void)mask;
	assert(state->depth == 1);
	++f->stops;
	if (f->stop_error)
		return f->stop_error;
	assert(f->references == 1);
	--f->references;
	f->controls_owned = false;
	return 0;
}
static int custom_legacy(struct v4l2_subdev *sd, int enable)
{
	struct fixture *f = sd->dev->fixture;
	if (enable) { ++f->references; ++f->starts; return f->start_error; }
	--f->references; ++f->stops;
	return f->stop_error;
}
/* Model the existing eager-release stop policy used by drivers such as IMX219:
 * the attempted stop reports an I/O error after releasing its PM/control state.
 * A second invocation must not be introduced by the legacy wrapper change.
 */
static int eager_disable(struct v4l2_subdev *sd, struct v4l2_subdev_state *state,
			 u32 pad, u64 mask)
{
	struct fixture *f = sd->dev->fixture;
	(void)pad; (void)mask;
	assert(state->depth == 1);
	++f->stops;
	--f->references;
	f->controls_owned = false;
	return f->stop_error;
}
static const struct v4l2_subdev_video_ops helper_video = { .s_stream = v4l2_subdev_s_stream_helper };
static const struct v4l2_subdev_video_ops legacy_video = { .s_stream = custom_legacy };
static const struct v4l2_subdev_video_ops wrapper_video = { .s_stream = call_s_stream };
static const struct v4l2_subdev_pad_ops pad_ops = { .enable_streams = fake_enable, .disable_streams = fake_disable };
static const struct v4l2_subdev_pad_ops eager_pad_ops = { .enable_streams = fake_enable, .disable_streams = eager_disable };
static const struct v4l2_subdev_ops helper_ops = { .video = &helper_video, .pad = &pad_ops };
static const struct v4l2_subdev_ops legacy_ops = { .video = &legacy_video };
static const struct v4l2_subdev_ops custom_with_pad_ops = { .video = &legacy_video, .pad = &pad_ops };
static const struct v4l2_subdev_ops eager_ops = { .video = &helper_video, .pad = &eager_pad_ops };
const struct v4l2_subdev_ops v4l2_subdev_call_wrappers = { .video = &wrapper_video };

static void init(struct fixture *f, bool streams, bool retry)
{
	memset(f, 0, sizeof(*f));
	pthread_mutexattr_t attr;
	assert(!pthread_mutexattr_init(&attr));
	assert(!pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK));
	assert(!pthread_mutex_init(&f->state.lock, &attr));
	assert(!pthread_mutexattr_destroy(&attr));
	f->dev.fixture = f; f->sd.dev = &f->dev; f->sd.ops = &helper_ops;
	f->sd.active_state = &f->state;
	f->sd.flags = (streams ? V4L2_SUBDEV_FL_STREAMS : 0) |
		(retry ? V4L2_SUBDEV_FL_STOP_RETRY : 0);
	f->sd.entity.name = "synthetic pad callback";
	f->sd.entity.num_pads = 1; f->sd.entity.pads = &f->pad;
	f->pad.flags = MEDIA_PAD_FL_SOURCE;
	f->sd.entity.graph_obj.mdev = &f->media; f->media.dev = &f->dev;
	f->state.stream_configs.num_configs = 2; f->state.stream_configs.configs = f->configs;
	f->state.routing.num_routes = 2; f->state.routing.routes = f->routes;
	for (unsigned int i = 0; i < 2; ++i) {
		f->configs[i].stream = i * 3; f->routes[i].source_stream = i * 3;
		f->routes[i].flags = V4L2_SUBDEV_ROUTE_FL_ACTIVE;
	}
}
static void done(struct fixture *f)
{ assert(!f->state.depth); assert(!pthread_mutex_destroy(&f->state.lock)); }
static u64 mask(struct fixture *f)
{ return (f->sd.flags & V4L2_SUBDEV_FL_STREAMS) ? BIT_ULL(0) | BIT_ULL(3) : BIT_ULL(0); }
static bool enabled(struct fixture *f)
{ return (f->sd.flags & V4L2_SUBDEV_FL_STREAMS) ? f->configs[0].enabled && f->configs[1].enabled : !!f->sd.enabled_pads; }
static int start(struct fixture *f, bool legacy)
{ return legacy ? v4l2_subdev_call(&f->sd, video, s_stream, 1) : v4l2_subdev_enable_streams(&f->sd, 0, mask(f)); }
static int stop(struct fixture *f, bool legacy)
{ return legacy ? v4l2_subdev_call(&f->sd, video, s_stream, 0) : v4l2_subdev_disable_streams(&f->sd, 0, mask(f)); }

static int helper_retry(void)
{
	for (int legacy = 0; legacy < 2; ++legacy)
	for (int streams = 0; streams < 2; ++streams)
	for (int error = 0; error < 2; ++error) {
		struct fixture f;
		init(&f, streams, true);
		CHECK("helper start", start(&f, legacy) == 0);
		CHECK("helper start ownership", enabled(&f) && f.references == 1 && f.controls_owned && f.privacy);
		f.stop_error = error ? -ETIMEDOUT : -EIO;
		for (int retry = 0; retry < 3; ++retry) {
			CHECK("helper stop errno", stop(&f, legacy) == f.stop_error);
			CHECK("legacy enabled state retained", f.sd.s_stream_enabled == !!legacy);
			CHECK("pad state and ownership retained", enabled(&f) && f.references == 1 && f.controls_owned && f.privacy);
		}
		CHECK("duplicate start policy", start(&f, legacy) == (legacy ? 0 : -EALREADY));
		CHECK("duplicate start owns one reference", f.starts == 1 && f.references == 1);
		f.stop_error = 0;
		CHECK("successful explicit stop retry", stop(&f, legacy) == 0);
		CHECK("retry releases ownership", !enabled(&f) && !f.sd.s_stream_enabled && !f.references && !f.controls_owned && !f.privacy);
		CHECK("restart", start(&f, legacy) == 0);
		CHECK("restart ownership", f.references == 1 && f.starts == 2);
		CHECK("final stop", stop(&f, legacy) == 0);
		CHECK("final reference balance", !f.references && !enabled(&f));
		done(&f);
	}
	return 0;
}
static int legacy_compatibility(void)
{
	for (int custom_pad = 0; custom_pad < 2; ++custom_pad) {
		struct fixture f;
		init(&f, false, true); f.sd.ops = custom_pad ? &custom_with_pad_ops : &legacy_ops;
		CHECK("legacy start", start(&f, true) == 0);
		f.stop_error = -EIO;
		CHECK("legacy stop compatibility", stop(&f, true) == 0);
		CHECK("legacy callback state", !f.sd.s_stream_enabled && !f.privacy && !f.references);
		done(&f);
	}
	return 0;
}
static int start_failure(void)
{
	for (int legacy = 0; legacy < 2; ++legacy)
	for (int streams = 0; streams < 2; ++streams) {
		struct fixture f;
		init(&f, streams, true); f.start_error = -EIO;
		CHECK("start errno", start(&f, legacy) == -EIO);
		CHECK("failed start leaves disabled", !enabled(&f) && !f.sd.s_stream_enabled && !f.references && !f.privacy);
		f.start_error = 0;
		CHECK("start retry", start(&f, legacy) == 0);
		CHECK("start retry stop", stop(&f, legacy) == 0);
		CHECK("start retry balance", !f.references && !enabled(&f));
		done(&f);
	}
	return 0;
}
static int validation_errors(void)
{
	struct fixture f;
	init(&f, false, true);
	CHECK("bad pad", v4l2_subdev_enable_streams(&f.sd, 1, 1) == -EINVAL);
	CHECK("missing stream", v4l2_subdev_enable_streams(&f.sd, 0, 2) == -EINVAL);
	CHECK("already disabled", v4l2_subdev_disable_streams(&f.sd, 0, 1) == -EALREADY);
	CHECK("validation invokes no callback", !f.starts && !f.stops && !f.references);
	CHECK("zero-mask start", v4l2_subdev_enable_streams(&f.sd, 0, 0) == 0);
	CHECK("zero-mask ownership", !f.references && !enabled(&f));
	done(&f);
	return 0;
}
static int eager_compatibility(void)
{
	for (int streams = 0; streams < 2; ++streams) {
		struct fixture f;
		init(&f, streams, false); f.sd.ops = &eager_ops;
		CHECK("eager start", start(&f, true) == 0);
		f.stop_error = -EIO;
		int first = stop(&f, true);
		int second = stop(&f, true);
		CHECK("eager helper single release", f.references == 0 && f.stops == 1);
		CHECK("eager helper legacy errno policy", first == 0 && second == 0);
		CHECK("eager helper flag cleared", !f.sd.s_stream_enabled && !f.privacy && !f.controls_owned);
		/* Core pad bookkeeping on failed eager stop is inherited; this change
		 * deliberately does not promise a modern-pad retry for unopted drivers.
		 */
		done(&f);
	}
	return 0;
}
int main(int argc, char **argv)
{
	struct { const char *name; int (*run)(void); } groups[] = {
		{ "helper-retry", helper_retry }, { "legacy-compatibility", legacy_compatibility },
		{ "start-failure", start_failure }, { "validation-errors", validation_errors },
		{ "eager-compatibility", eager_compatibility },
	};
	unsigned int ran = 0;
	for (unsigned int i = 0; i < ARRAY_SIZE(groups); ++i) {
		if (argc > 1 && strcmp(argv[1], groups[i].name)) continue;
		if (groups[i].run()) return 1;
		printf("PASS %s\n", groups[i].name); ++ran;
	}
	if (!ran) return 2;
	printf("PASS groups=%u assertions=%u\n", ran, assertions);
	return 0;
}
