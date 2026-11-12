/* SPDX-License-Identifier: GPL-2.0-only */
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stddef.h>
typedef uint32_t u32;
#define BIT(n) (1U << (n))
#define FIELD_GET(mask, value) ((value) & (mask))
#define GFP_KERNEL 0
#define GPIOD_ASIS 0
#define USB_TYPEC_DP_SID 0xff01
#define USB_TYPEC_TBT_SID 0x8087
#define TBT_CABLE_ACTIVE_PASSIVE BIT(0)
#define TBT_ENTER_MODE_UNI_DIR_LSRX BIT(1)
#define EUDO_CABLE_TYPE_MASK 3
#define EUDO_CABLE_TYPE_PASSIVE 0
#define TYPEC_DP_STATE_C 10
#define TYPEC_DP_STATE_D 11
#define TYPEC_DP_STATE_E 12
#define TYPEC_DP_STATE_F 13
#define TYPEC_STATE_SAFE 20
#define TYPEC_MODE_USB2 21
#define TYPEC_STATE_USB 22
#define TYPEC_MODE_USB3 23
#define TYPEC_MODE_USB4 24
#define fallthrough __attribute__((fallthrough))
#define dev_err(dev, ...) ((void)(dev))
#define dev_info(dev, ...) ((void)(dev))
#define IS_ERR(p) ((uintptr_t)(p) >= (uintptr_t)-4095)
#define PTR_ERR(p) ((int)(intptr_t)(p))
#define ERR_PTR(e) ((void *)(intptr_t)(e))
struct device { int ignored; };
struct gpio_desc { int ignored; };
struct regulator { int index; };
struct clk { int ignored; };
struct regmap { u32 reg[32]; };
struct mutex { int held; };
struct i2c_client { struct device dev; void *data; };
enum typec_orientation { TYPEC_ORIENTATION_NONE, TYPEC_ORIENTATION_NORMAL, TYPEC_ORIENTATION_REVERSE };
struct typec_altmode { unsigned int svid; };
struct typec_thunderbolt_data { unsigned int cable_mode, enter_vdo; };
struct enter_usb_data { unsigned int eudo; };
struct typec_retimer_state { struct typec_altmode *alt; unsigned long mode; void *data; };
struct typec_mux_state { struct typec_altmode *alt; unsigned long mode; void *data; };
struct typec_switch { int ignored; };
struct typec_mux { int ignored; };
struct typec_switch_dev { void *drvdata; };
struct typec_retimer { void *drvdata; };
struct typec_switch_desc { void *drvdata, *fwnode; int (*set)(struct typec_switch_dev *, enum typec_orientation); };
struct typec_retimer_desc { void *drvdata, *fwnode; int (*set)(struct typec_retimer *, struct typec_retimer_state *); };
struct regmap_config { int max_register, reg_bits, val_bits; };
struct reset_controller_dev;
struct reset_control_ops { int (*reset)(struct reset_controller_dev *, unsigned long); int (*assert)(struct reset_controller_dev *, unsigned long); int (*deassert)(struct reset_controller_dev *, unsigned long); };
struct reset_controller_dev { const struct reset_control_ops *ops; };
struct qcom_reset_map { unsigned long reg; u32 bitmask; unsigned int bit, udelay; };
struct qcom_reset_controller { struct reset_controller_dev rcdev; struct regmap *regmap; const struct qcom_reset_map *reset_map; };
#define to_qcom_reset_controller(p) ((struct qcom_reset_controller *)(p))
struct fixture {
 int failed, tests, enable_attempt, fail_enable, supply[6], bad_disable;
 int xo, fail_xo, fail_switch, fail_retimer, fail_read, configured;
 int writes, assigns, fail_write_reg, fail_assign, upstream_error, upstream_calls;
 int gpio, gpio_sets, switch_unreg, retimer_unreg, mux_calls;
 int update_error, read_error, updates, reads, assert_calls, deassert_calls, assert_error, deassert_error;
 unsigned int delay;
} f;
static struct regulator supplies[6] = {{0},{1},{2},{3},{4},{5}};
static struct regmap map;
static struct clk clock_source;
static struct gpio_desc gpio;
static struct typec_switch upstream;
static struct typec_mux mux;
static struct typec_switch_dev registered_sw;
static struct typec_retimer registered_retimer;
static void *allocation;
static const char *case_name;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: %s\n", case_name, __LINE__, #c); f.failed++; } } while (0)
static void begin(const char *name) {
 int failed=f.failed, tests=f.tests+1;
 memset(&f,0,sizeof(f)); memset(&map,0,sizeof(map));
 f.failed=failed;f.tests=tests;f.fail_write_reg=-1; f.configured=1; case_name=name;
}
static void mutex_init(struct mutex *m) { m->held=0; }
static void mutex_lock(struct mutex *m) { CHECK(!m->held);m->held=1; }
static void mutex_unlock(struct mutex *m) { CHECK(m->held);m->held=0; }
struct mutex_guard { struct mutex *lock; };
static struct mutex_guard lock_guard(struct mutex *m) { mutex_lock(m);return (struct mutex_guard){m}; }
static void unlock_guard(struct mutex_guard *g) { mutex_unlock(g->lock); }
#define guard(kind) struct mutex_guard __attribute__((cleanup(unlock_guard))) _guard = lock_guard
static void fsleep(unsigned int delay) { f.delay=delay; }
static void usleep_range(unsigned int a,unsigned int b) { (void)a;(void)b; }
static void msleep(unsigned int a) { (void)a; }
static int regulator_enable(struct regulator *r) { if (++f.enable_attempt==f.fail_enable)return -EIO;f.supply[r->index]++;return 0; }
static int regulator_disable(struct regulator *r) { if (--f.supply[r->index]<0)f.bad_disable++;return 0; }
static void gpiod_set_value(struct gpio_desc *g,int v) { (void)g;f.gpio=v;f.gpio_sets++; }
static int gpiod_direction_output(struct gpio_desc *g,int v) { gpiod_set_value(g,v);return 0; }
static int regmap_write(struct regmap *m,unsigned int r,u32 v) { f.writes++;if ((int)r==f.fail_write_reg)return -EIO;m->reg[r]=v;return 0; }
static int regmap_assign_bits(struct regmap *m,unsigned int r,u32 mask,bool value) { f.assigns++;if (f.fail_assign)return -EIO;m->reg[r]=(m->reg[r]&~mask)|(value?mask:0);return 0; }
static int regmap_update_bits(struct regmap *m,unsigned int r,u32 mask,u32 v) { f.updates++;if(f.update_error)return f.update_error;m->reg[r]=(m->reg[r]&~mask)|(v&mask);return 0; }
static int regmap_read(struct regmap *m,unsigned int r,u32 *v) { f.reads++;if(f.read_error)return f.read_error;if(f.fail_read)return -ENXIO;*v=m->reg[r];return 0; }
static int regmap_test_bits(struct regmap *m,unsigned int r,u32 mask) { (void)m;(void)r;(void)mask;return f.configured; }
static int typec_switch_set(struct typec_switch *sw,enum typec_orientation orientation) { (void)sw;(void)orientation;f.upstream_calls++;return f.upstream_error; }
static void *typec_switch_get_drvdata(struct typec_switch_dev *sw) { return sw->drvdata; }
static void *typec_retimer_get_drvdata(struct typec_retimer *r) { return r->drvdata; }
static int typec_mux_set(struct typec_mux *m,struct typec_mux_state *s) { (void)m;(void)s;f.mux_calls++;return 0; }
static void *devm_kzalloc(struct device *dev,size_t size,int flags) { (void)dev;(void)flags;allocation=calloc(1,size);return allocation; }
static struct regulator *devm_regulator_get(struct device *dev,const char *name) {
 (void)dev;const char *names[]={"vdd","vdd33","vdd33-cap","vddat","vddar","vddio"};
 for(int i=0;i<6;i++)if(!strcmp(name,names[i]))return &supplies[i];abort();
}
static int dev_err_probe(struct device *dev,int error,const char *msg) { (void)dev;(void)msg;return error; }
static struct regmap *devm_regmap_init_i2c(struct i2c_client *c,const struct regmap_config *cfg) { (void)c;(void)cfg;return &map; }
static struct clk *devm_clk_get(struct device *dev,const char *name) { (void)dev;(void)name;return &clock_source; }
static struct gpio_desc *devm_gpiod_get(struct device *dev,const char *name,int flags) { (void)dev;(void)name;(void)flags;return &gpio; }
static struct typec_switch *typec_switch_get(struct device *dev) { (void)dev;return &upstream; }
static struct typec_mux *typec_mux_get(struct device *dev) { (void)dev;return &mux; }
static void typec_switch_put(struct typec_switch *sw) { (void)sw; }
static void typec_mux_put(struct typec_mux *m) { (void)m; }
static int drm_aux_bridge_register(struct device *dev) { (void)dev;return 0; }
static int clk_prepare_enable(struct clk *c) { (void)c;if(f.fail_xo)return -EIO;f.xo++;return 0; }
static void clk_disable_unprepare(struct clk *c) { (void)c;f.xo--; }
static void *dev_fwnode(struct device *dev) { return dev; }
static struct typec_switch_dev *typec_switch_register(struct device *dev,const struct typec_switch_desc *d) { (void)dev;if(f.fail_switch)return ERR_PTR(-ENOMEM);registered_sw.drvdata=d->drvdata;return &registered_sw; }
static struct typec_retimer *typec_retimer_register(struct device *dev,const struct typec_retimer_desc *d) { (void)dev;if(f.fail_retimer)return ERR_PTR(-ENOMEM);registered_retimer.drvdata=d->drvdata;return &registered_retimer; }
static void typec_switch_unregister(struct typec_switch_dev *sw) { (void)sw;f.switch_unreg++; }
static void typec_retimer_unregister(struct typec_retimer *r) { (void)r;f.retimer_unreg++; }
static void i2c_set_clientdata(struct i2c_client *c,void *data) { c->data=data; }
static void *i2c_get_clientdata(struct i2c_client *c) { return c->data; }
static bool of_machine_is_compatible(const char *compat) { return !strcmp(compat,"qcom,x1e80100"); }
