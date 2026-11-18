/* SPDX-License-Identifier: GPL-2.0-only */
static int mock_assert(struct reset_controller_dev *c,unsigned long id) { (void)c;(void)id;f.assert_calls++;return f.assert_error; }
static int mock_deassert(struct reset_controller_dev *c,unsigned long id) { (void)c;(void)id;f.deassert_calls++;return f.deassert_error; }
static void test_reset(void) {
 const struct qcom_reset_map maps[]={{.reg=4,.bit=3},{.reg=5,.bitmask=0x30,.udelay=12}};
 const struct reset_control_ops ops={.assert=mock_assert,.deassert=mock_deassert};
 struct qcom_reset_controller rst={.rcdev={&ops},.regmap=&map,.reset_map=maps};
 begin("reset-assert-error");f.assert_error=-EBUSY;
 CHECK(qcom_reset(&rst.rcdev,0)==-EBUSY);CHECK(f.assert_calls==1);CHECK(!f.deassert_calls);CHECK(!f.delay);
 begin("reset-deassert-error");f.deassert_error=-ETIMEDOUT;
 CHECK(qcom_reset(&rst.rcdev,1)==-ETIMEDOUT);CHECK(f.assert_calls==1&&f.deassert_calls==1);CHECK(f.delay==12);
 begin("reset-default-delay");CHECK(!qcom_reset(&rst.rcdev,0));CHECK(f.delay==1);
 begin("reset-write-error");f.update_error=-EIO;CHECK(qcom_reset_assert(&rst.rcdev,0)==-EIO);CHECK(f.updates==1);CHECK(!f.reads);
 begin("reset-readback-error");f.read_error=-ETIMEDOUT;CHECK(qcom_reset_deassert(&rst.rcdev,1)==-ETIMEDOUT);CHECK(f.updates==1&&f.reads==1);
 begin("reset-bit-and-bitmask");CHECK(!qcom_reset_assert(&rst.rcdev,0));CHECK(map.reg[4]==8);CHECK(!qcom_reset_assert(&rst.rcdev,1));CHECK(map.reg[5]==0x30);CHECK(!qcom_reset_deassert(&rst.rcdev,1));CHECK(map.reg[5]==0);
}
static void init_retimer(struct ps883x_retimer *r,struct i2c_client *client,bool powered) {
 memset(r,0,sizeof(*r));r->client=client;r->regmap=&map;r->reset_gpio=&gpio;r->xo_clk=&clock_source;
 r->typec_switch=&upstream;r->typec_mux=&mux;r->vdd_supply=&supplies[0];r->vdd33_supply=&supplies[1];r->vdd33_cap_supply=&supplies[2];r->vddat_supply=&supplies[3];r->vddar_supply=&supplies[4];r->vddio_supply=&supplies[5];
 r->in_reset=!powered;for(int i=0;i<6;i++)f.supply[i]=powered;
}
static void supplies_are(int n) { for(int i=0;i<6;i++)CHECK(f.supply[i]==n);CHECK(!f.bad_disable); }
static void test_orientation(void) {
 struct i2c_client client={0};struct ps883x_retimer r;struct typec_switch_dev sw={&r};
 begin("orientation-retry-after-failure");init_retimer(&r,&client,true);r.orientation=TYPEC_ORIENTATION_NORMAL;r.orientation_valid=true;f.fail_assign=1;
 CHECK(ps883x_sw_set(&sw,TYPEC_ORIENTATION_REVERSE)==-EIO);CHECK(r.orientation==TYPEC_ORIENTATION_NORMAL);CHECK(!r.orientation_valid);CHECK(f.assigns==1);
 f.fail_assign=0;CHECK(!ps883x_sw_set(&sw,TYPEC_ORIENTATION_REVERSE));CHECK(f.assigns==2);CHECK(r.orientation_valid&&r.orientation==TYPEC_ORIENTATION_REVERSE);CHECK(map.reg[0]&CONN_STATUS_0_ORIENTATION_REVERSED);
 CHECK(!ps883x_sw_set(&sw,TYPEC_ORIENTATION_REVERSE));CHECK(f.assigns==2);CHECK(!r.lock.held);
 begin("orientation-upstream-error");init_retimer(&r,&client,true);f.upstream_error=-ENOTCONN;CHECK(ps883x_sw_set(&sw,TYPEC_ORIENTATION_REVERSE)==-ENOTCONN);CHECK(!f.assigns);CHECK(r.orientation==TYPEC_ORIENTATION_NONE);
 begin("orientation-cache-in-reset");init_retimer(&r,&client,false);CHECK(!ps883x_sw_set(&sw,TYPEC_ORIENTATION_REVERSE));CHECK(r.orientation==TYPEC_ORIENTATION_REVERSE&&!r.orientation_valid);CHECK(!f.assigns);
 struct typec_retimer_state state={.mode=TYPEC_MODE_USB3};CHECK(!ps883x_set(&r,&state));CHECK(!r.in_reset&&r.orientation_valid);CHECK(map.reg[0]&CONN_STATUS_0_ORIENTATION_REVERSED);supplies_are(1);
 begin("orientation-reset-invalidation");init_retimer(&r,&client,true);r.orientation_valid=true;ps883x_reset(&r);CHECK(r.in_reset&&!r.orientation_valid);supplies_are(0);int count=f.gpio_sets;ps883x_reset(&r);CHECK(f.gpio_sets==count);supplies_are(0);
 begin("orientation-invalid-same-value");init_retimer(&r,&client,true);r.orientation=TYPEC_ORIENTATION_NORMAL;r.orientation_valid=false;CHECK(!ps883x_sw_set(&sw,TYPEC_ORIENTATION_NORMAL));CHECK(f.assigns==1&&r.orientation_valid);
 for(int reg=0;reg<3;reg++) {
  char name[80];snprintf(name,sizeof(name),"configure-register-%d-error",reg);begin(name);init_retimer(&r,&client,true);r.orientation_valid=true;f.fail_write_reg=reg;
  CHECK(ps883x_configure(&r,0x23,0x04,0x80,false)==-EIO);CHECK(f.writes==reg+1);CHECK(r.orientation_valid==(reg!=0));supplies_are(1);
 }
 begin("usb4-policy-guard");init_retimer(&r,&client,true);r.disable_usb4=true;state.mode=TYPEC_MODE_USB4;CHECK(ps883x_set(&r,&state)==-EOPNOTSUPP);CHECK(!f.writes);
 begin("usb4-platform-guard");CHECK(ps883x_should_disable_usb4());
}
static void test_regulators(void) {
 struct i2c_client client={0};struct ps883x_retimer r;
 for(int at=1;at<=6;at++) {
  char name[80];snprintf(name,sizeof(name),"configure-regulator-%d-error",at);begin(name);init_retimer(&r,&client,false);f.fail_enable=at;
  CHECK(ps883x_configure(&r,0,0,0,false)==-EIO);CHECK(r.in_reset);CHECK(!f.writes);supplies_are(0);
  f.fail_enable=0;CHECK(!ps883x_configure(&r,0,0,0,false));CHECK(!r.in_reset);supplies_are(1);ps883x_reset(&r);supplies_are(0);
 }
}
static void test_probe_remove(void) {
 for(int mode=0;mode<7;mode++) {
  char name[80];snprintf(name,sizeof(name),"probe-unwind-%d",mode);begin(name);struct i2c_client client={0};
  if(mode==0)f.fail_xo=1;
  if(mode==1){f.configured=0;f.fail_read=1;}
  if(mode==2)f.fail_switch=1;
  if(mode==3)f.fail_retimer=1;
  if(mode==4)f.fail_enable=4;
  int ret=ps883x_retimer_probe(&client);
  if(mode<5){CHECK(ret<0);supplies_are(0);CHECK(!f.xo);}
  else {
   CHECK(!ret);struct ps883x_retimer *r=client.data;CHECK(r&&r->in_reset);supplies_are(0);CHECK(f.xo==1);
   if(mode==6){struct typec_retimer_state state={.mode=TYPEC_MODE_USB3};CHECK(!ps883x_set(r,&state));supplies_are(1);}
   ps883x_retimer_remove(&client);supplies_are(0);CHECK(!f.xo);CHECK(f.switch_unreg==1&&f.retimer_unreg==1);
  }
  free(allocation);allocation=NULL;
 }
}
int main(void) { test_reset();test_orientation();test_regulators();test_probe_remove();printf("%d behavioral cases; %d failures\n",f.tests,f.failed);return f.failed?1:0; }
