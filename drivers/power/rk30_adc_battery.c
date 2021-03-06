/* drivers/power/rk30_adc_battery.c
 *
 * battery detect driver for the rk30 
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/module.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/regulator/consumer.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <asm/io.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <mach/gpio.h>
#include <linux/adc.h>
#include <mach/iomux.h>
#include <mach/board.h>
#include <linux/delay.h>
#include <linux/ktime.h>
#include <linux/slab.h>
#include <linux/syscalls.h>
#include <linux/fs.h>
#include <linux/wakelock.h>

#if 0
#define DBG(x...)   printk(x)
#else
#define DBG(x...)
#endif

static int rk30_battery_dbg_level = 0;
#if defined(CONFIG_BATTERY_AOSBIS_CAPACITY99_CHECK)
static int batt_capacity99_check_count=0;
static int batt_forced_full = 0;
#define   NUM_CHARGE_CAP99_FULL_DELAY_TIMES         ((3600 * 1000) / TIMER_MS_COUNTS)
#endif
module_param_named(dbg_level, rk30_battery_dbg_level, int, 0644);
#define pr_bat( args...) \
	do { \
		if (rk30_battery_dbg_level) { \
			pr_info(args); \
		} \
	} while (0)


/*******************以下参数可以修改******************************/
#define	TIMER_MS_COUNTS		 1000	//定时器的长度ms

//以下参数需要根据实际测试调整
#define	SLOPE_SECOND_COUNTS	               15	//统计电压斜率的时间间隔s
#define	DISCHARGE_MIN_SECOND	               45	//最快放电电1%时间
#define	CHARGE_MIN_SECOND	               45	//最快充电电1%时间
#define	CHARGE_MID_SECOND	               90	//普通充电电1%时间
#define	CHARGE_MAX_SECOND	               250	//最长充电电1%时间
#define   CHARGE_FULL_DELAY_TIMES          10          //充电满检测防抖时间
#define   USBCHARGE_IDENTIFY_TIMES        5           //插入USB混流，pc识别检测时间

#define	NUM_VOLTAGE_SAMPLE	                       ((SLOPE_SECOND_COUNTS * 1000) / TIMER_MS_COUNTS)	 
#define	NUM_DISCHARGE_MIN_SAMPLE	         ((DISCHARGE_MIN_SECOND * 1000) / TIMER_MS_COUNTS)	 
#define	NUM_CHARGE_MIN_SAMPLE	         ((CHARGE_MIN_SECOND * 1000) / TIMER_MS_COUNTS)	    
#define	NUM_CHARGE_MID_SAMPLE	         ((CHARGE_MID_SECOND * 1000) / TIMER_MS_COUNTS)	     
#define	NUM_CHARGE_MAX_SAMPLE	         ((CHARGE_MAX_SECOND * 1000) / TIMER_MS_COUNTS)	  
#define   NUM_CHARGE_FULL_DELAY_TIMES         ((CHARGE_FULL_DELAY_TIMES * 1000) / TIMER_MS_COUNTS)	//充电满状态持续时间长度
#define   NUM_USBCHARGE_IDENTIFY_TIMES      ((USBCHARGE_IDENTIFY_TIMES * 1000) / TIMER_MS_COUNTS)	//充电满状态持续时间长度

#define BAT_2V5_VALUE	                                     2500

#define BATT_FILENAME "/data/bat_last_capacity.dat"

static struct wake_lock batt_wake_lock;


struct batt_vol_cal{
	u32 disp_cal;
	u32 dis_charge_vol;
	u32 charge_vol;
};

#ifdef CONFIG_BATTERY_RK30_VOL3V8

#ifdef CONFIG_BATTERY_BT_B0BFN_3474107
#define BATT_MAX_VOL_VALUE				4110               	//满电时的电池电压
#define BATT_ZERO_VOL_VALUE				3500              	//关机时的电池电压
#define BATT_NOMAL_VOL_VALUE			3800
#define CHARGE_OFFSET						10
#endif

#ifdef CONFIG_BATTERY_BT_B0B6G
#define BATT_MAX_VOL_VALUE				4110                	//满电时的电池电压
#define BATT_ZERO_VOL_VALUE				3500              	//关机时的电池电压
#define BATT_NOMAL_VOL_VALUE			3800
#define CHARGE_OFFSET						10
#endif

#ifdef CONFIG_BATTERY_BT_C0B5
#define BATT_MAX_VOL_VALUE				4154                	//满电时的电池电压
#define BATT_ZERO_VOL_VALUE				3500              	//关机时的电池电压
#define BATT_NOMAL_VOL_VALUE			3800
#define CHARGE_OFFSET						10
#endif

#ifdef CONFIG_BATTERY_BT_C0B4
#define BATT_MAX_VOL_VALUE				4147		//满电时的电池电压    
#define BATT_ZERO_VOL_VALUE				3500		//关机时的电池电压    
#define BATT_NOMAL_VOL_VALUE			3800
#define CHARGE_OFFSET						0
#endif

#ifdef CONFIG_BATTERY_BT_C0B5H
#define BATT_MAX_VOL_VALUE                             4179               	//Full  charge volate
#define BATT_ZERO_VOL_VALUE                            3500              	//power down voltage
#define BATT_NOMAL_VOL_VALUE                         3800
#define BATT_CHARGE_OFFSET				10
#endif

//divider resistance 
#define BAT_PULL_UP_R                                         200
#define BAT_PULL_DOWN_R                                    200

#ifdef CONFIG_BATTERY_BT_B0BFN_3474107
static struct batt_vol_cal batt_table[] = {
	{0,3500,3590},	{10,3565,3836},{20,3615,3908},{30,3642,3934},{40,3669,3957},{50,3693,3985},
	{60,3733,4018},{70,3782,4061},{80,3838,4082},{90,3903,4097},{100,3970,4110},
};
#endif
#ifdef CONFIG_BATTERY_BT_B0B6G
static struct batt_vol_cal  batt_table[] = {
	{0,3500,3560},{1,3510,3570},{2,3515,3580},{3,3520,3600},{5,3535,3620},{7,3550,3630},
	{9,3565,3650},{11,3580,3670},{13,3595,3680},{15,3610,3700},{17,3635,3710},
	{19,3655,3720},{21,3670,3740},{23,3685,3745},{25,3700,3770},{27,3710,3775},
	{29,3720,3780},{31,3730,3790},{33,3735,3792},{35,3738,3795},{37,3740,3800},
	{39,3746,3805},{41,3750,3809},{43,3754,3811},{45,3759,3813},{47,3764,3819},
	{49,3771,3820},{51,3775,3825},{53,3780,3826},{55,3786,3830},{57,3796,3832},
	{59,3800,3835},{61,3808,3838},{63,3819,3846},{65,3823,3850},{67,3835,3865},
	{69,3846,3878},{71,3862,3893},{73,3873,3914},{75,3886,3931},{77,3899,3939},
	{79,3916,3970},{81,3926,4007},{83,3944,4030},{85,3955,4050},{87,3970,4068},
	{89,3993,4092},{91,4006,4112},{93,4020,4136},{95,4054,4159},{97,4060,4177},{100,4120,4200},
};
#endif
#ifdef CONFIG_BATTERY_BT_B0BDN_3574108
static struct batt_vol_cal  batt_table[] = {
	{0,3500,3590},	{10,3565,3836},{20,3615,3908},{30,3642,3934},{40,3669,3957},{50,3693,3985},
	{60,3733,4018},{70,3782,4061},{80,3838,4082},{90,3903,4097},{100,3970,4110},
};

static struct batt_vol_cal  batt_table_2[] ={
	{0,3500,3590},	{10,3565,3836},{20,3615,3908},{30,3642,3934},{40,3669,3957},{50,3693,3985},
	{60,3733,4018},{70,3782,4061},{80,3838,4082},{90,3903,4097},{100,3970,4110},
};
#endif
#ifdef CONFIG_BATTERY_BT_C0B5
static struct batt_vol_cal  batt_table[] ={
	{0,3500,3650},{10,3536,3889},{20,3563,3985},{30,3587,4039},{40,3613,4092},{50,3656,4125},
	{60,3692,4135},{70,3754,4142},{80,3817,4148},{90,3885,4150},{100,4123,4154},
};
#endif
 #ifdef CONFIG_BATTERY_BT_C0B4
static struct batt_vol_cal  batt_table[] ={
	{0,3500,3642},{10,3512,3920},{20,3539,3960},{30,3567,3993},{40,3599,4012},{50,3643,4060},
	{60,3684,4113},{70,3761,4122},{80,3832,4128},{90,3914,4135},{100,4088,4143},
};
#endif

#ifdef CONFIG_BATTERY_BT_C0B5H
static struct batt_vol_cal  batt_table[] ={
	{0,3510,3614},{10,3571,3934},{20,3583,3974},{30,3595,3999},{40,3647,4033},{50,3676,4079},
	{60,3730,4138},{70,3784,4171},{80,3847,4172},{90,3918,4176},{100,4130,4179},
};
#endif
#else
#define BATT_MAX_VOL_VALUE                              8284              	//Full charge voltage
#define BATT_ZERO_VOL_VALUE                             6800            	// power down voltage 
#define BATT_NOMAL_VOL_VALUE                          7600                

//定义ADC采样分压电阻，以实际值为准，单位K

#define BAT_PULL_UP_R                                         300 
#define BAT_PULL_DOWN_R                                    100

static struct batt_vol_cal  batt_table[] = {
	{0,6800,7400},    {1,6840,7440},    {2,6880,7480},     {3,6950,7450},       {5,7010,7510},    {7,7050,7550},
	{9,7080,7580},    {11,7104,7604},   {13,7140,7640},   {15,7160,7660},      {17,7220,7720},
	{19,7260,7760},  {21,7280,7780},   {23,7304,7802},   {25,7324,7824},      {27,7344,7844},
	{29,7360,7860},  {31,7374,7874},   {33,7386,7886},   {35,7398,7898},      {37,7410,7910},//500
	{39,7420,7920},  {41,7424,7928},   {43,7436,7947},   {45,7444,7944},      {47,7450,7958}, //508
	{49,7460,7965},  {51,7468,7975},   {53, 7476,7990},  {55,7482,8000},      {57,7492,8005}, // 5 14
	{59,7500,8011},  {61,7510,8033},   {63,7528,8044},   {65,7548,8055},      {67,7560,8066},//506
	{69,7600,8070},  {71,7618,8075},   {73,7634,8080},   {75,7654,8085},      {77,7690,8100}, //400
	{79,7900,8180},  {81,7920,8210},   {83,7964,8211},   {85,8000,8214},      {87,8002,8218},//290
	{89,8012, 8220}, {91,8022,8235},   {93,8110,8260},   {95,8140,8290},       {97,8170,8300},  {100,8200 ,8310},//110

};
#endif


#define BATT_NUM  ARRAY_SIZE(batt_table)
extern int __sramdata g_pmic_type;

#define adc_to_voltage(adc_val)                           ((adc_val * BAT_2V5_VALUE * (BAT_PULL_UP_R + BAT_PULL_DOWN_R)) / (1024 * BAT_PULL_DOWN_R))

/********************************************************************************/

extern int dwc_vbus_status(void);
extern int get_msc_connect_flag(void);
extern int dwc_otg_check_dpdm(void);
static int usb_status_flag = 0;
#ifdef CONFIG_BATTERY_RK30_USB_AND_CHARGE
extern int usb_enum_st;
#endif
extern int power_off_status = 0;

struct rk30_adc_battery_data {
	int irq;
	
	//struct timer_list       timer;
	struct workqueue_struct *wq;
	struct delayed_work 	    delay_work;
	struct work_struct 	    dcwakeup_work;
	struct work_struct                   lowerpower_work;
	bool                    resume;
	
	struct rk30_adc_battery_platform_data *pdata;

	int                     full_times;
	
	struct adc_client       *client; 
	int                     adc_val;
	int                     adc_samples[NUM_VOLTAGE_SAMPLE+2];
	
	int                     bat_status;
	int                     bat_status_cnt;
	int                     bat_health;
	int                     bat_present;
	int                     bat_voltage;
	int                     bat_capacity;
	int                     bat_change;
	
	int                     old_charge_level;
	int                    *pSamples;
	int                     gBatCapacityDisChargeCnt;
	int                     gBatCapacityChargeCnt;
	int 	          capacitytmp;
	int                     poweron_check;
	int                     suspend_capacity;

	int                     status_lock;
#if defined (CONFIG_BATTERY_RK30_USB_CHARGE)
	int	usb_status;
	int	usb_old_satus;
#endif

};
static struct rk30_adc_battery_data *gBatteryData;

enum {
	BATTERY_STATUS          = 0,
	BATTERY_HEALTH          = 1,
	BATTERY_PRESENT         = 2,
	BATTERY_CAPACITY        = 3,
	BATTERY_AC_ONLINE       = 4,
	BATTERY_STATUS_CHANGED	= 5,
	AC_STATUS_CHANGED   	= 6,
	BATTERY_INT_STATUS	    = 7,
	BATTERY_INT_ENABLE	    = 8,
};

typedef enum {
	CHARGER_BATTERY = 0,
	CHARGER_USB,
	CHARGER_AC
} charger_type_t;

bool is_ac_charge = false;
bool is_usb_charge = false;
volatile bool low_usb_charge = false;

bool is_accharging(void)
{
	return is_ac_charge;
}
bool is_usbcharging(void)
{
	return is_usb_charge;
}
bool low_usb_charging(void)
{
	return low_usb_charge;
}



static int rk30_adc_battery_load_capacity(void)
{
	char value[4];
	int* p = (int *)value;
	long fd = sys_open(BATT_FILENAME,O_RDONLY,0);

	if(fd < 0){
		pr_bat("rk30_adc_battery_load_capacity: open file /data/bat_last_capacity.dat failed\n");
		return -1;
	}

	sys_read(fd,(char __user *)value,4);
	sys_close(fd);

	return (*p);
}

static void rk30_adc_battery_put_capacity(int loadcapacity)
{
	char value[4];
	int* p = (int *)value;
	long fd = sys_open(BATT_FILENAME,O_CREAT | O_RDWR,0);

	if(fd < 0){
		pr_bat("rk30_adc_battery_put_capacity: open file /data/bat_last_capacity.dat failed\n");
		return;
	}
	
	*p = loadcapacity;
	sys_write(fd, (const char __user *)value, 4);

	sys_close(fd);
}

static void rk30_adc_battery_charge_enable(struct rk30_adc_battery_data *bat)
{
	struct rk30_adc_battery_platform_data *pdata = bat->pdata;

	if (pdata->charge_set_pin != INVALID_GPIO){
		gpio_direction_output(pdata->charge_set_pin, pdata->charge_set_level);
	}
}

static void rk30_adc_battery_charge_disable(struct rk30_adc_battery_data *bat)
{
	struct rk30_adc_battery_platform_data *pdata = bat->pdata;

	if (pdata->charge_set_pin != INVALID_GPIO){
		gpio_direction_output(pdata->charge_set_pin, 1 - pdata->charge_set_level);
	}
}
#ifdef CONFIG_BATTERY_RK30_USB_AND_CHARGE
static void rk30_ac_charge_current_set(struct rk30_adc_battery_data *bat)
{
	struct rk30_adc_battery_platform_data *pdata = bat->pdata;
	int status;

	if (pdata->charge_type_pin != INVALID_GPIO){
		status = gpio_get_value(pdata->charge_type_pin);
		if(status == 0)
			gpio_direction_output(pdata->charge_type_pin, 1);
	}
}

static void rk30_usb_charge_current_set(struct rk30_adc_battery_data *bat)
{
	struct rk30_adc_battery_platform_data *pdata = bat->pdata;
	int status;

	if (pdata->charge_type_pin != INVALID_GPIO){
		status = gpio_get_value(pdata->charge_type_pin);
		if(status == 1)
			gpio_direction_output(pdata->charge_type_pin, 0);
	}
}
#endif
#if defined(CONFIG_BATTERY_RK30_USB_CHARGE)
static int rk30_adc_battery_get_usb_property(struct power_supply *psy,
				    enum power_supply_property psp,
				    union power_supply_propval *val);

static enum power_supply_property rk30_adc_battery_usb_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
};

static struct power_supply rk30_usb_supply =
{
	.name = "usb",
	.type = POWER_SUPPLY_TYPE_USB,

	.get_property   = rk30_adc_battery_get_usb_property,

	.properties     = rk30_adc_battery_usb_props,
	.num_properties = ARRAY_SIZE(rk30_adc_battery_usb_props),
};
#endif

//extern int suspend_flag;
static int rk30_adc_battery_get_charge_level(struct rk30_adc_battery_data *bat)
{
	int charge_on = 0;
	struct rk30_adc_battery_platform_data *pdata = bat->pdata;

#if defined (CONFIG_BATTERY_RK30_AC_CHARGE)
	if (pdata->dc_det_pin != INVALID_GPIO){
		if (gpio_get_value (pdata->dc_det_pin) == pdata->dc_det_level){
			charge_on = 1;
#if defined  (CONFIG_BATTERY_RK30_USB_CHARGE)
			bat->usb_status = 0;
#endif
		}
	}
#endif

#if defined  (CONFIG_BATTERY_RK30_USB_CHARGE)
	if (pdata->usb_det_pin != INVALID_GPIO){
		if (gpio_get_value (pdata->usb_det_pin) == pdata->usb_det_level){
					charge_on = 1;
		#ifdef CONFIG_BATTERY_RK30_USB_AND_CHARGE
			if(usb_enum_st == 1)
				bat->usb_status  = 1;
			else
				bat->usb_status  = 0;
		#else
			bat->usb_status  = 1;
		#endif
		}                   
		else{
			bat->usb_status = 0;
		#ifdef CONFIG_BATTERY_RK30_USB_AND_CHARGE
			usb_enum_st = 0;
		#endif
			}

		if( (bat->usb_status) != (bat->usb_old_satus)){
			power_supply_changed(&rk30_usb_supply);
			bat->usb_old_satus = bat->usb_status;
		}
	}
	usb_status_flag = bat->usb_status;
#endif

	return charge_on;
}

//int old_charge_level;
static int rk30_adc_battery_status_samples(struct rk30_adc_battery_data *bat)
{
	int charge_level;
	
	struct rk30_adc_battery_platform_data *pdata = bat->pdata;

	charge_level = rk30_adc_battery_get_charge_level(bat);

	//检测充电状态变化情况
	if (charge_level != bat->old_charge_level){
		bat->old_charge_level = charge_level;
		bat->bat_change  = 1;
		
		if(charge_level) {            
			rk30_adc_battery_charge_enable(bat);
		#ifdef CONFIG_BATTERY_RK30_USB_AND_CHARGE
			if(usb_enum_st == 1)
				rk30_usb_charge_current_set(bat);
			else if(usb_enum_st == 0)
				rk30_ac_charge_current_set(bat);
		#endif
		}
		else{
			rk30_adc_battery_charge_disable(bat);
		}
		bat->bat_status_cnt = 0;        //状态变化开始计数
	}

	if(charge_level == 0){   
	//discharge
		bat->full_times = 0;
#if defined(CONFIG_BATTERY_AOSBIS_CAPACITY99_CHECK)
              batt_capacity99_check_count = 0;
              batt_forced_full = 0;
#endif
		bat->bat_status = POWER_SUPPLY_STATUS_NOT_CHARGING;
	}
	else{
	//CHARGE	    
		if (pdata->charge_ok_pin == INVALID_GPIO){  //no charge_ok_pin

			if (bat->bat_capacity == 100){
				if (bat->bat_status != POWER_SUPPLY_STATUS_FULL){
					bat->bat_status = POWER_SUPPLY_STATUS_FULL;
					bat->bat_change  = 1;
				}
			}
			else{
				bat->bat_status = POWER_SUPPLY_STATUS_CHARGING;
			}
		}
		else{  // pin of charge_ok_pin
#if defined(CONFIG_BATTERY_AOSBIS_CAPACITY99_CHECK)
			if (gpio_get_value(pdata->charge_ok_pin) != pdata->charge_ok_level){
				bat->full_times = 0;

				if(bat->bat_capacity >= 99)
					batt_capacity99_check_count++;
				else
					batt_capacity99_check_count=0;

				if (batt_capacity99_check_count >= NUM_CHARGE_CAP99_FULL_DELAY_TIMES) {
					batt_capacity99_check_count = NUM_CHARGE_CAP99_FULL_DELAY_TIMES + 1;
				}


				if ((batt_capacity99_check_count >= NUM_CHARGE_CAP99_FULL_DELAY_TIMES) && (bat->bat_capacity >= 99)){
					if (bat->bat_status != POWER_SUPPLY_STATUS_FULL){
						bat->bat_status = POWER_SUPPLY_STATUS_FULL;
						bat->bat_capacity = 100;
						batt_forced_full = 1;
						bat->bat_change  = 1;
					}
				}
				else{
					bat->bat_status = POWER_SUPPLY_STATUS_CHARGING;
				}
			}
			else{
	//检测到充电满电平标志
				bat->full_times++;
				if(bat->bat_capacity >= 99)
					batt_capacity99_check_count++;
				else
					batt_capacity99_check_count=0;

				if (batt_capacity99_check_count >= NUM_CHARGE_CAP99_FULL_DELAY_TIMES) {
					batt_capacity99_check_count = NUM_CHARGE_CAP99_FULL_DELAY_TIMES + 1;
				}

				if (bat->full_times >= NUM_CHARGE_FULL_DELAY_TIMES) {
					bat->full_times = NUM_CHARGE_FULL_DELAY_TIMES + 1;
				}

				if (((bat->full_times >= NUM_CHARGE_FULL_DELAY_TIMES) || (batt_capacity99_check_count >= NUM_CHARGE_CAP99_FULL_DELAY_TIMES)) && (bat->bat_capacity >= 99)){
					if (bat->bat_status != POWER_SUPPLY_STATUS_FULL){
						bat->bat_status = POWER_SUPPLY_STATUS_FULL;
						bat->bat_capacity = 100;
						bat->bat_change  = 1;
					}
				}
				else{
					bat->bat_status = POWER_SUPPLY_STATUS_CHARGING;
				}
			}
#else
			if (gpio_get_value(pdata->charge_ok_pin) != pdata->charge_ok_level){
				bat->full_times = 0;
				bat->bat_status = POWER_SUPPLY_STATUS_CHARGING;
			}
			else{
	//检测到充电满电平标志
				bat->full_times++;

				if (bat->full_times >= NUM_CHARGE_FULL_DELAY_TIMES) {
					bat->full_times = NUM_CHARGE_FULL_DELAY_TIMES + 1;
				}

				if ((bat->full_times >= NUM_CHARGE_FULL_DELAY_TIMES) && (bat->bat_capacity >= 99)){
					if (bat->bat_status != POWER_SUPPLY_STATUS_FULL){
						bat->bat_status = POWER_SUPPLY_STATUS_FULL;
						bat->bat_capacity = 100;
						bat->bat_change  = 1;
					}
				}
				else{
					bat->bat_status = POWER_SUPPLY_STATUS_CHARGING;
				}
			}
#endif
		}
	}

	return charge_level;
}

static int *pSamples;
static void rk30_adc_battery_voltage_samples(struct rk30_adc_battery_data *bat)
{
	int value;
	int i,*pStart = bat->adc_samples, num = 0;
	int level = rk30_adc_battery_get_charge_level(bat);

	struct batt_vol_cal *p;
#ifdef CONFIG_BATTERY_BT_B0BDN_3574108
	if(1 == g_pmic_type)
		p = batt_table;
	else if(2 == g_pmic_type)
		p = batt_table_2;
#else
	p = batt_table;
#endif

	value = bat->adc_val;
	adc_async_read(bat->client);

	*pSamples++ = adc_to_voltage(value);

	bat->bat_status_cnt++;
	if (bat->bat_status_cnt > NUM_VOLTAGE_SAMPLE)  bat->bat_status_cnt = NUM_VOLTAGE_SAMPLE + 1;

	num = pSamples - pStart;
	
	if (num >= NUM_VOLTAGE_SAMPLE){
		pSamples = pStart;
		num = NUM_VOLTAGE_SAMPLE;
		
	}

	value = 0;
	for (i = 0; i < num; i++){
		value += bat->adc_samples[i];
	}
	bat->bat_voltage = value / num;

	/*消除毛刺电压*/
	if(1 == level){
		if(bat->bat_voltage >= p[BATT_NUM-1].charge_vol+ 10)
			bat->bat_voltage = p[BATT_NUM-1].charge_vol  + 10;
		else if(bat->bat_voltage <= p[0].charge_vol  - 10)
			bat->bat_voltage =  p[0].charge_vol - 10;
	}
	else{
		if(bat->bat_voltage >= p[BATT_NUM-1].dis_charge_vol+ 10)
			bat->bat_voltage = p[BATT_NUM-1].dis_charge_vol  + 10;
		else if(bat->bat_voltage <= p[0].dis_charge_vol  - 10)
			bat->bat_voltage =  p[0].dis_charge_vol - 10;

	}

}
static int rk30_adc_battery_voltage_to_capacity(struct rk30_adc_battery_data *bat, int BatVoltage)
{
	int i = 0;
	int capacity = 0;
	struct rk30_adc_battery_platform_data *pdata = bat->pdata;

	struct batt_vol_cal *p;
#ifdef CONFIG_BATTERY_BT_B0BDN_3574108
	if(1 == g_pmic_type)
		p = batt_table;
	else if(2 == g_pmic_type)
		p = batt_table_2;
#else
	p = batt_table;
#endif


	if (rk30_adc_battery_get_charge_level(bat)){  //charge
		if(BatVoltage >= (p[BATT_NUM - 1].charge_vol)){
			capacity = 100;
		}	
		else{
			if(BatVoltage <= (p[0].charge_vol)){
				capacity = 0;
			}
			else{
				for(i = 0; i < BATT_NUM - 1; i++){
					if(1 == gpio_get_value(pdata->back_light_pin))
						BatVoltage = BatVoltage + CHARGE_OFFSET;

					if(((p[i].charge_vol) <= BatVoltage) && (BatVoltage < (p[i+1].charge_vol))){
						capacity = p[i].disp_cal + ((BatVoltage - p[i].charge_vol) *  (p[i+1].disp_cal -p[i].disp_cal ))/ (p[i+1].charge_vol- p[i].charge_vol);
						break;
					}
				}
			}  
		}

	}
	else{  //discharge
		if(BatVoltage >= (p[BATT_NUM - 1].dis_charge_vol)){
			capacity = 100;
		}	
		else{
			if(BatVoltage <= (p[0].dis_charge_vol)){
				capacity = 0;
			}
			else{
				for(i = 0; i < BATT_NUM - 1; i++){
					if(((p[i].dis_charge_vol) <= BatVoltage) && (BatVoltage < (p[i+1].dis_charge_vol))){
						capacity =  p[i].disp_cal+ ((BatVoltage - p[i].dis_charge_vol) * (p[i+1].disp_cal -p[i].disp_cal ) )/ (p[i+1].dis_charge_vol- p[i].dis_charge_vol) ;
						break;
					}
				}
			}  

		}


	}
    return capacity;
}

static void rk30_adc_battery_capacity_samples(struct rk30_adc_battery_data *bat)
{
	int capacity = 0;
	struct rk30_adc_battery_platform_data *pdata = bat->pdata;
	int timer_of_discharge_sample = NUM_DISCHARGE_MIN_SAMPLE;
	int timer_of_charge_sample = NUM_CHARGE_MIN_SAMPLE;

	//充放电状态变化后，Buffer填满之前，不更新
	if (bat->bat_status_cnt < NUM_VOLTAGE_SAMPLE)  {
		bat->gBatCapacityDisChargeCnt = 0;
		bat->gBatCapacityChargeCnt    = 0;
		return;
	}
	
	capacity = rk30_adc_battery_voltage_to_capacity(bat, bat->bat_voltage);
	    
	if (rk30_adc_battery_get_charge_level(bat)){
		if (capacity > bat->bat_capacity){
			if(capacity > bat->bat_capacity + 10 )
			        timer_of_charge_sample = NUM_CHARGE_MIN_SAMPLE -10;
			else if(capacity > bat->bat_capacity + 7 )
			        timer_of_charge_sample = NUM_CHARGE_MIN_SAMPLE -5;
			else if(capacity > bat->bat_capacity + 3 )
			        timer_of_charge_sample = NUM_CHARGE_MIN_SAMPLE -2;

			//实际采样到的容量比显示的容量大，逐级上升
			if (++(bat->gBatCapacityDisChargeCnt) >= timer_of_charge_sample){
				bat->gBatCapacityDisChargeCnt  = 0;
				if (bat->bat_capacity < 99){
					bat->bat_capacity++;
					bat->bat_change  = 1;
				}
			}
			bat->gBatCapacityChargeCnt = 0;
		}
		else{  //   实际的容量比采样比 显示的容量小
		            bat->gBatCapacityDisChargeCnt = 0;
		            (bat->gBatCapacityChargeCnt)++;
            
			if (pdata->charge_ok_pin != INVALID_GPIO){
				if (gpio_get_value(pdata->charge_ok_pin) == pdata->charge_ok_level){
				//检测到电池充满标志，同时长时间内充电电压无变化，开始启动计时充电，快速上升容量
					if (bat->gBatCapacityChargeCnt >= NUM_CHARGE_MIN_SAMPLE){
						bat->gBatCapacityChargeCnt = 0;
						if (bat->bat_capacity < 99){
							bat->bat_capacity++;
							bat->bat_change  = 1;
						}
					}
				}
				else{
#if 0					
					if (capacity > capacitytmp){
					//过程中如果电压有增长，定时器复位，防止定时器模拟充电比实际充电快
						gBatCapacityChargeCnt = 0;
					}
					else if (/*bat->bat_capacity >= 85) &&*/ (gBatCapacityChargeCnt > NUM_CHARGE_MAX_SAMPLE)){
						gBatCapacityChargeCnt = (NUM_CHARGE_MAX_SAMPLE - NUM_CHARGE_MID_SAMPLE);

						if (bat->bat_capacity < 99){
						bat->bat_capacity++;
						bat->bat_change  = 1;
						}
					}
				}
#else			//  防止电池老化后出现冲不满的情况，
					if (capacity > bat->capacitytmp){
					//过程中如果电压有增长，定时器复位，防止定时器模拟充电比实际充电快
						bat->gBatCapacityChargeCnt = 0;
					}
					else{

						if ((bat->bat_capacity >= 85) &&((bat->gBatCapacityChargeCnt) > NUM_CHARGE_MAX_SAMPLE)){
							bat->gBatCapacityChargeCnt = (NUM_CHARGE_MAX_SAMPLE - NUM_CHARGE_MID_SAMPLE);

							if (bat->bat_capacity < 99){
								bat->bat_capacity++;
								bat->bat_change  = 1;
							}
						}
					}
				}
#endif

			}
			else{
			//没有充电满检测脚，长时间内电压无变化，定时器模拟充电
				if (capacity > bat->capacitytmp){
				//过程中如果电压有增长，定时器复位，防止定时器模拟充电比实际充电快
					bat->gBatCapacityChargeCnt = 0;
				}
				else{

					if ((bat->bat_capacity >= 85) &&(bat->gBatCapacityChargeCnt > NUM_CHARGE_MAX_SAMPLE)){
						bat->gBatCapacityChargeCnt = (NUM_CHARGE_MAX_SAMPLE - NUM_CHARGE_MID_SAMPLE);

						if (bat->bat_capacity < 99){
							bat->bat_capacity++;
							bat->bat_change  = 1;
						}
					}
				}
				

			}            
		}
	}    
	else{   
	//放电时,只允许电压下降
		if (capacity < bat->bat_capacity){
			if(capacity + 3 > bat->bat_capacity  )
			        timer_of_discharge_sample = NUM_DISCHARGE_MIN_SAMPLE -5;
			else if(capacity  + 7 > bat->bat_capacity )
			        timer_of_discharge_sample = NUM_DISCHARGE_MIN_SAMPLE -10;
			else if(capacity  + 10> bat->bat_capacity )
		                timer_of_discharge_sample = NUM_DISCHARGE_MIN_SAMPLE - 15;

			if (++(bat->gBatCapacityDisChargeCnt) >= timer_of_discharge_sample){
				bat->gBatCapacityDisChargeCnt = 0;
				if (bat->bat_capacity > 0){
					bat->bat_capacity-- ;
					bat->bat_change  = 1;
				}
			}
		}
		else{
			bat->gBatCapacityDisChargeCnt = 0;
		}
		bat->gBatCapacityChargeCnt = 0;
	}

#if defined(CONFIG_BATTERY_AOSBIS_CAPACITY99_CHECK)
    if((batt_forced_full == 1) && (bat->bat_capacity==99))
        bat->bat_capacity=100;
#endif
		bat->capacitytmp = capacity;
}

//static int poweron_check = 0;
static void rk30_adc_battery_poweron_capacity_check(void)
{

	int new_capacity, old_capacity;

	new_capacity = gBatteryData->bat_capacity;
	old_capacity = rk30_adc_battery_load_capacity();
	if ((old_capacity < 0) || (old_capacity > 100)){
		old_capacity = new_capacity;
	}
	if (gBatteryData->bat_status == POWER_SUPPLY_STATUS_FULL){
		if (new_capacity > 80){
			gBatteryData->bat_capacity = 100;
		}
	}
	else
		gBatteryData->bat_capacity = old_capacity;

#if 0
	else if (gBatteryData->bat_status != POWER_SUPPLY_STATUS_NOT_CHARGING){
	//chargeing state
	//问题：
//	//1）长时间关机放置后，开机后读取的容量远远大于实际容量怎么办？
//	//2）如果不这样做，短时间关机再开机，前后容量不一致又该怎么办？
//	//3）一下那种方式合适？
	gBatteryData->bat_capacity = old_capacity;
//		gBatteryData->bat_capacity = (new_capacity > old_capacity) ? new_capacity : old_capacity;
//		gBatteryData->bat_capacity = (new_capacity > old_capacity) ? old_capacity : new_capacity;

	}else{

		if(new_capacity > old_capacity + 50 )
			gBatteryData->bat_capacity = new_capacity;
		else
			gBatteryData->bat_capacity = (new_capacity < old_capacity) ? new_capacity : old_capacity;  //avoid the value of capacity increase 
	}
#endif

	//printk("capacity = %d, new_capacity = %d, old_capacity = %d\n",gBatteryData->bat_capacity, new_capacity, old_capacity);

	gBatteryData->bat_change = 1;
}

#if defined(CONFIG_BATTERY_RK30_USB_CHARGE)
static int rk30_adc_battery_get_usb_property(struct power_supply *psy, 
				    enum power_supply_property psp,
				    union power_supply_propval *val)
{
	charger_type_t charger;
	charger =  CHARGER_USB;

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
			if (psy->type == POWER_SUPPLY_TYPE_USB){
				//val->intval = get_msc_connect_flag();
				if (gBatteryData->pdata->usb_det_pin != INVALID_GPIO){
					if (gpio_get_value (gBatteryData->pdata->usb_det_pin) == gBatteryData->pdata->usb_det_level){
					#ifdef CONFIG_BATTERY_RK30_USB_AND_CHARGE
						if(usb_status_flag == 1)
							val->intval =  1;
						else
							val->intval =  0;
					#else
						val->intval =  1;
					#endif
					}
					else
						val->intval =  0;
				}
			}

			DBG("usb_propert   val->intval ==== %d\n",val->intval);
		break;

	default:
		return -EINVAL;
	}
	
	return 0;

}
#endif

#if defined(CONFIG_BATTERY_RK30_AC_CHARGE)
static irqreturn_t rk30_adc_battery_dc_wakeup(int irq, void *dev_id)
{   
	disable_irq_nosync(irq);
	queue_work(gBatteryData->wq, &gBatteryData->dcwakeup_work);
	return IRQ_HANDLED;
}


static int rk30_adc_battery_get_ac_property(struct power_supply *psy,
			enum power_supply_property psp,
			union power_supply_propval *val)
{
	int ret = 0;
	charger_type_t charger;
	charger =  CHARGER_USB;
	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		if (psy->type == POWER_SUPPLY_TYPE_MAINS)
		{
		#ifdef CONFIG_BATTERY_RK30_USB_AND_CHARGE
			if (rk30_adc_battery_get_charge_level(gBatteryData) && (usb_status_flag == 0))
		#else
			if (gpio_get_value (gBatteryData->pdata->dc_det_pin) == gBatteryData->pdata->dc_det_level)
		#endif
			{
				val->intval = 1;
				}
			else
				{
				val->intval = 0;	
				}
		}
		DBG("%s:%d\n",__FUNCTION__,val->intval);
		break;
		
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static enum power_supply_property rk30_adc_battery_ac_props[] = 
{
	POWER_SUPPLY_PROP_ONLINE,
};

static struct power_supply rk30_ac_supply = 
{
	.name = "ac",
	.type = POWER_SUPPLY_TYPE_MAINS,

	.get_property   = rk30_adc_battery_get_ac_property,

	.properties     = rk30_adc_battery_ac_props,
	.num_properties = ARRAY_SIZE(rk30_adc_battery_ac_props),
};

static void rk30_adc_battery_dcdet_delaywork(struct work_struct *work)
{
	int ret;
	struct rk30_adc_battery_platform_data *pdata;
	int irq;
	int irq_flag;
	
	rk28_send_wakeup_key(); // wake up the system
	msleep(10);

	pdata    = gBatteryData->pdata;
	irq        = gpio_to_irq(pdata->dc_det_pin);
	irq_flag = gpio_get_value (pdata->dc_det_pin) ? IRQF_TRIGGER_FALLING : IRQF_TRIGGER_RISING;

//	rk28_send_wakeup_key(); // wake up the system

	free_irq(irq, NULL);
	ret = request_irq(irq, rk30_adc_battery_dc_wakeup, irq_flag, "ac_charge_irq", NULL);// reinitialize the DC irq 
	if (ret) {
		free_irq(irq, NULL);
	}

#ifdef CONFIG_BATTERY_RK30_USB_CHARGE
	power_supply_changed(&rk30_usb_supply);
#endif
	power_supply_changed(&rk30_ac_supply);

	gBatteryData->bat_status_cnt = 0;        //the state of battery is change

}


#endif

static int rk30_adc_battery_get_status(struct rk30_adc_battery_data *bat)
{
	return (bat->bat_status);
}

static int rk30_adc_battery_get_health(struct rk30_adc_battery_data *bat)
{
	return POWER_SUPPLY_HEALTH_GOOD;
}

static int rk30_adc_battery_get_present(struct rk30_adc_battery_data *bat)
{
	return (bat->bat_voltage < BATT_MAX_VOL_VALUE) ? 0 : 1;
}

static int rk30_adc_battery_get_voltage(struct rk30_adc_battery_data *bat)
{
	return (bat->bat_voltage );
}

static int rk30_adc_battery_get_capacity(struct rk30_adc_battery_data *bat)
{
	return (bat->bat_capacity);
}

static int rk30_adc_battery_get_property(struct power_supply *psy,
				 enum power_supply_property psp,
				 union power_supply_propval *val)
{		
	int ret = 0;

	switch (psp) {
		case POWER_SUPPLY_PROP_STATUS:
			val->intval = rk30_adc_battery_get_status(gBatteryData);
			DBG("gBatStatus=%d\n",val->intval);
			break;
		case POWER_SUPPLY_PROP_HEALTH:
			val->intval = rk30_adc_battery_get_health(gBatteryData);
			DBG("gBatHealth=%d\n",val->intval);
			break;
		case POWER_SUPPLY_PROP_PRESENT:
			val->intval = rk30_adc_battery_get_present(gBatteryData);
			DBG("gBatPresent=%d\n",val->intval);
			break;
		case POWER_SUPPLY_PROP_VOLTAGE_NOW:
			val ->intval = rk30_adc_battery_get_voltage(gBatteryData);
			DBG("gBatVoltage=%d\n",val->intval);
			break;
		//	case POWER_SUPPLY_PROP_CURRENT_NOW:
		//		val->intval = 1100;
		//		break;
		case POWER_SUPPLY_PROP_CAPACITY:
			val->intval = rk30_adc_battery_get_capacity(gBatteryData);
			DBG("gBatCapacity=%d%%\n",val->intval);
			break;
		case POWER_SUPPLY_PROP_TECHNOLOGY:
			val->intval = POWER_SUPPLY_TECHNOLOGY_LION;	
			break;
		case POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN:
			val->intval = BATT_MAX_VOL_VALUE;
			break;
		case POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN:
			val->intval = BATT_ZERO_VOL_VALUE;
			break;
		default:
			ret = -EINVAL;
			break;
	}

	return ret;
}

static enum power_supply_property rk30_adc_battery_props[] = {

	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
//	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN,
	POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN,
};

static struct power_supply rk30_battery_supply = 
{
	.name = "battery",
	.type = POWER_SUPPLY_TYPE_BATTERY,

	.get_property   = rk30_adc_battery_get_property,

	.properties     = rk30_adc_battery_props,
	.num_properties = ARRAY_SIZE(rk30_adc_battery_props),
};

#ifdef CONFIG_PM
static void rk30_adc_battery_resume_check(void)
{
	int i;
	int level,oldlevel;
	int new_capacity, old_capacity;
	struct rk30_adc_battery_data *bat = gBatteryData;

	bat->old_charge_level = -1;
	pSamples = bat->adc_samples;

	adc_sync_read(bat->client);                             //start adc sample
	level = oldlevel = rk30_adc_battery_status_samples(bat);//init charge status

	for (i = 0; i < NUM_VOLTAGE_SAMPLE; i++) {               //0.3 s   
	
		mdelay(1);
		rk30_adc_battery_voltage_samples(bat);              //get voltage
		level = rk30_adc_battery_status_samples(bat);       //check charge status
		if (oldlevel != level){		
		    oldlevel = level;                               //if charge status changed, reset sample
		    i = 0;
		}        
	}
	new_capacity = rk30_adc_battery_voltage_to_capacity(bat, bat->bat_voltage);
	old_capacity =gBatteryData-> suspend_capacity;

//	if (bat->bat_status != POWER_SUPPLY_STATUS_NOT_CHARGING){
	//chargeing state
//		bat->bat_capacity = (new_capacity > old_capacity) ? new_capacity : old_capacity;
//	}
//	else{
		bat->bat_capacity = (new_capacity < old_capacity) ? new_capacity : old_capacity;  // aviod the value of capacity increase    dicharge
//	}

}

static int rk30_adc_battery_suspend(struct platform_device *dev, pm_message_t state)
{
	int irq;
	gBatteryData->suspend_capacity = gBatteryData->bat_capacity;
	cancel_delayed_work(&gBatteryData->delay_work);
	
	if( gBatteryData->pdata->batt_low_pin != INVALID_GPIO){
		
		irq = gpio_to_irq(gBatteryData->pdata->batt_low_pin);
		enable_irq(irq);
	    	enable_irq_wake(irq);
    	}

	return 0;
}

static int rk30_adc_battery_resume(struct platform_device *dev)
{
	int irq;
	gBatteryData->resume = true;
	queue_delayed_work(gBatteryData->wq, &gBatteryData->delay_work, msecs_to_jiffies(100));
	if( gBatteryData->pdata->batt_low_pin != INVALID_GPIO){
		
		irq = gpio_to_irq(gBatteryData->pdata->batt_low_pin);
	    	disable_irq_wake(irq);
		disable_irq(irq);
    	}
	return 0;
}
#else
#define rk30_adc_battery_suspend NULL
#define rk30_adc_battery_resume NULL
#endif


unsigned long AdcTestCnt = 0;
static int num = 0;
static void rk30_adc_battery_check(struct rk30_adc_battery_data *bat);

static void rk30_adc_battery_timer_work(struct work_struct *work)
{
#ifdef CONFIG_PM
	if (gBatteryData->resume) {
		rk30_adc_battery_resume_check();
		gBatteryData->resume = false;
	}
#endif
//	if(!num){
//		rk30_adc_battery_check(gBatteryData);
//		num++;
//	}
	rk30_adc_battery_status_samples(gBatteryData);

	if (gBatteryData->poweron_check){   
		gBatteryData->poweron_check = 0;
		rk30_adc_battery_poweron_capacity_check();
	}

	rk30_adc_battery_voltage_samples(gBatteryData);
	rk30_adc_battery_capacity_samples(gBatteryData);

	if( 1 == rk30_adc_battery_get_charge_level(gBatteryData)){  // charge
		if(0 == gBatteryData->status_lock ){			
			wake_lock(&batt_wake_lock);  //lock
			gBatteryData->status_lock = 1; 
			printk("---------usb in---\n");
		}
	}
	else{
		if(1 == gBatteryData->status_lock ){			
			printk("---------usb out---\n");
			mdelay(2000);
			wake_unlock(&batt_wake_lock);  //unlock
			gBatteryData->status_lock = 0; 
		}

	}
	
	
	/*update battery parameter after adc and capacity has been changed*/
	if(gBatteryData->bat_change){
		gBatteryData->bat_change = 0;
		rk30_adc_battery_put_capacity(gBatteryData->bat_capacity);
		power_supply_changed(&rk30_battery_supply);
	}

	if (rk30_battery_dbg_level){
		if (++AdcTestCnt >= 2)
			{
			AdcTestCnt = 0;

			printk("Status = %d, RealAdcVal = %d, RealVol = %d,gBatVol = %d, gBatCap = %d, RealCapacity = %d, dischargecnt = %d, chargecnt = %d\n", 
			gBatteryData->bat_status, gBatteryData->adc_val, adc_to_voltage(gBatteryData->adc_val), 
			gBatteryData->bat_voltage, gBatteryData->bat_capacity, gBatteryData->capacitytmp, gBatteryData->gBatCapacityDisChargeCnt,gBatteryData-> gBatCapacityChargeCnt);

		}
	}
	queue_delayed_work(gBatteryData->wq, &gBatteryData->delay_work, msecs_to_jiffies(TIMER_MS_COUNTS));

}


static int rk30_adc_battery_io_init(struct rk30_adc_battery_platform_data *pdata)
{
	int ret = 0;
	
	if (pdata->io_init) {
		pdata->io_init();
	}
	
	//charge control pin
	if (pdata->charge_set_pin != INVALID_GPIO){
	    	ret = gpio_request(pdata->charge_set_pin, NULL);
	    	if (ret) {
	    		printk("failed to request dc_det gpio\n");
	    		goto error;
		    	}
	    	gpio_direction_output(pdata->charge_set_pin, 1 - pdata->charge_set_level);
	}
#ifdef CONFIG_BATTERY_RK30_USB_AND_CHARGE
	//charge current set pin
	if (pdata->charge_type_pin != INVALID_GPIO){
		ret = gpio_request(pdata->charge_type_pin, NULL);
		if (ret) {
			printk("failed to request charge_type_pin gpio\n");
			goto error;
		}
		gpio_direction_output(pdata->charge_type_pin, 0);
	}
#endif
	
	//dc charge detect pin
	if (pdata->dc_det_pin != INVALID_GPIO){
	    	ret = gpio_request(pdata->dc_det_pin, NULL);
	    	if (ret) {
	    		printk("failed to request dc_det gpio\n");
	    		goto error;
	    	}
	
	    	gpio_pull_updown(pdata->dc_det_pin, GPIOPullUp);//important
	    	ret = gpio_direction_input(pdata->dc_det_pin);
	    	if (ret) {
	    		printk("failed to set gpio dc_det input\n");
	    		goto error;
	    	}
	}
	
	//charge ok detect
	if (pdata->charge_ok_pin != INVALID_GPIO){
 		ret = gpio_request(pdata->charge_ok_pin, NULL);
	    	if (ret) {
	    		printk("failed to request charge_ok gpio\n");
	    		goto error;
	    	}
	
	    	gpio_pull_updown(pdata->charge_ok_pin, GPIOPullUp);//important
	    	ret = gpio_direction_input(pdata->charge_ok_pin);
	    	if (ret) {
	    		printk("failed to set gpio charge_ok input\n");
	    		goto error;
	    	}
	}
	//batt low pin
	if( pdata->batt_low_pin != INVALID_GPIO){
 		ret = gpio_request(pdata->batt_low_pin, NULL);
	    	if (ret) {
	    		printk("failed to request batt_low_pin gpio\n");
	    		goto error;
	    	}
	
	    	gpio_pull_updown(pdata->batt_low_pin, GPIOPullUp); 
	    	ret = gpio_direction_input(pdata->batt_low_pin);
	    	if (ret) {
	    		printk("failed to set gpio batt_low_pin input\n");
	    		goto error;
	    	}
	}
    
	return 0;
error:
	return -1;
}

//extern void kernel_power_off(void);
static void rk30_adc_battery_check(struct rk30_adc_battery_data *bat)
{
	int i;
	int level,oldlevel;
	int bat_old_status = POWER_SUPPLY_STATUS_NOT_CHARGING;
	struct rk30_adc_battery_platform_data *pdata = bat->pdata;
	//printk("%s--%d:\n",__FUNCTION__,__LINE__);

	struct batt_vol_cal *p;
#ifdef CONFIG_BATTERY_BT_B0BDN_3574108
	if(1 == g_pmic_type)
		p = batt_table;
	else if(2 == g_pmic_type)
		p = batt_table_2;
#else
	p = batt_table;
#endif

	bat->old_charge_level = -1;
	bat->capacitytmp = 0;
	bat->suspend_capacity = 0;
	
	pSamples = bat->adc_samples;

	adc_sync_read(bat->client);                             //start adc sample
	level = oldlevel = rk30_adc_battery_get_charge_level(bat);//init charge status

	bat->full_times = 0;
#if defined(CONFIG_BATTERY_AOSBIS_CAPACITY99_CHECK)
       batt_capacity99_check_count = 0;
       batt_forced_full = 0;
#endif
	do{
	for (i = 0; i < NUM_VOLTAGE_SAMPLE; i++){                //0.3 s
		mdelay(1);
		rk30_adc_battery_voltage_samples(bat);              //get voltage
		//level = rk30_adc_battery_status_samples(bat);       //check charge status
		level = rk30_adc_battery_get_charge_level(bat);

		if (oldlevel != level){
			oldlevel = level;                               //if charge status changed, reset sample
			i = 0;
		}        
	}
	bat->bat_capacity = rk30_adc_battery_voltage_to_capacity(bat, bat->bat_voltage);  //init bat_capacity

	bat->bat_status = POWER_SUPPLY_STATUS_NOT_CHARGING;
	if (rk30_adc_battery_get_charge_level(bat)){
#if  defined (CONFIG_BATTERY_RK30_USB_AND_CHARGE)
		if( 0 != dwc_otg_check_dpdm() )
			bat->bat_status = POWER_SUPPLY_STATUS_CHARGING;

		if(dwc_otg_check_dpdm() == 1){
			rk30_usb_charge_current_set(bat);
		}else if(dwc_otg_check_dpdm() == 2){
			rk30_ac_charge_current_set(bat);
		}
#endif
		bat->bat_status = POWER_SUPPLY_STATUS_CHARGING;
#if 0
		if (pdata->charge_ok_pin != INVALID_GPIO){
			if (gpio_get_value(pdata->charge_ok_pin) == pdata->charge_ok_level){
				bat->bat_status = POWER_SUPPLY_STATUS_FULL;
				bat->bat_capacity = 100;
			}
		}
#endif
	}

	if((bat->bat_status == POWER_SUPPLY_STATUS_NOT_CHARGING) && (bat_old_status == POWER_SUPPLY_STATUS_CHARGING))
		power_off_status = 1;
	bat_old_status = bat->bat_status;

	mdelay(300);
#ifdef CONFIG_BATTERY_RK30_USB_AND_CHARGE
	}while((bat->bat_voltage < p[0].charge_vol) && (bat->bat_status == POWER_SUPPLY_STATUS_CHARGING));
#else
	}while((bat->bat_voltage < p[0].charge_vol) &&
		(gpio_get_value (gBatteryData->pdata->dc_det_pin) != gBatteryData->pdata->dc_det_level));
#endif


#if 0
	rk30_adc_battery_poweron_capacity_check();
#else
	gBatteryData->poweron_check = 1;
#endif
	//gBatteryData->poweron_check = 0;

/*******************************************
//开机采样到的电压和上次关机保存电压相差较大，怎么处理？
if (bat->bat_capacity > old_capacity)
{
if ((bat->bat_capacity - old_capacity) > 20)
{

}
}
else if (bat->bat_capacity < old_capacity)
{
if ((old_capacity > bat->bat_capacity) > 20)
{

}
}
*********************************************/
	if (bat->bat_capacity == 0) bat->bat_capacity = 1;


#if 0
	if ((bat->bat_voltage <= batt_table[0].dis_charge_vol+ 50)&&(bat->bat_status != POWER_SUPPLY_STATUS_CHARGING)){
		kernel_power_off();
	}
#endif
}

static void rk30_adc_battery_callback(struct adc_client *client, void *param, int result)
{
#if 0
	struct rk30_adc_battery_data  *info = container_of(client, struct rk30_adc_battery_data,
		client);
	info->adc_val = result;
#endif
	if (result < 0){
		pr_bat("adc_battery_callback    resule < 0 , the value ");
		return;
	}
	else{
		gBatteryData->adc_val = result;
		pr_bat("result = %d, gBatteryData->adc_val = %d\n", result, gBatteryData->adc_val );
	}
	return;
}

#if 1
static void rk30_adc_battery_lowerpower_delaywork(struct work_struct *work)
{
	int irq;
	if( gBatteryData->pdata->batt_low_pin != INVALID_GPIO){
		irq = gpio_to_irq(gBatteryData->pdata->batt_low_pin);
		disable_irq(irq);
	}

	printk("lowerpower\n");
 	rk28_send_wakeup_key(); // wake up the system	
	return;
}


static irqreturn_t rk30_adc_battery_low_wakeup(int irq,void *dev_id)
{
	queue_work(gBatteryData->wq, &gBatteryData->lowerpower_work);
	return IRQ_HANDLED;
}

#endif

static int rk30_adc_battery_probe(struct platform_device *pdev)
{
	int    ret;
	int    irq;
	int    irq_flag;
	struct adc_client                   *client;
	struct rk30_adc_battery_data          *data;
	struct rk30_adc_battery_platform_data *pdata = pdev->dev.platform_data;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (data == NULL) {
		ret = -ENOMEM;
		goto err_data_alloc_failed;
	}
	gBatteryData = data;

	platform_set_drvdata(pdev, data);

   	data->pdata = pdata;
	data->status_lock = 0; 	
	ret = rk30_adc_battery_io_init(pdata);
	 if (ret) {
	 	goto err_io_init;
	}
    
	memset(data->adc_samples, 0, sizeof(int)*(NUM_VOLTAGE_SAMPLE + 2));

	 //register adc for battery sample
	client = adc_register(0, rk30_adc_battery_callback, NULL);  //pdata->adc_channel = ani0
	if(!client)
		goto err_adc_register_failed;
	    
	 //variable init
	data->client  = client;
	data->adc_val = adc_sync_read(client);

	ret = power_supply_register(&pdev->dev, &rk30_battery_supply);
	if (ret){
		printk(KERN_INFO "fail to battery power_supply_register\n");
		goto err_battery_failed;
	}
		

#if defined (CONFIG_BATTERY_RK30_USB_CHARGE)
	ret = power_supply_register(&pdev->dev, &rk30_usb_supply);
	data->usb_status = 0;
	data->usb_old_satus = 0;
	if (ret){
		printk(KERN_INFO "fail to usb power_supply_register\n");
		goto err_usb_failed;
	}
#endif
 	wake_lock_init(&batt_wake_lock, WAKE_LOCK_SUSPEND, "batt_lock");	

	data->wq = create_singlethread_workqueue("adc_battd");
	INIT_DELAYED_WORK(&data->delay_work, rk30_adc_battery_timer_work);

	//queue_delayed_work(data->wq, &data->delay_work, msecs_to_jiffies(TIMER_MS_COUNTS));
	queue_delayed_work(data->wq, &data->delay_work, msecs_to_jiffies(TIMER_MS_COUNTS*10));

#if  defined (CONFIG_BATTERY_RK30_AC_CHARGE)
	ret = power_supply_register(&pdev->dev, &rk30_ac_supply);
	if (ret) {
		printk(KERN_INFO "fail to ac power_supply_register\n");
		goto err_ac_failed;
	}
	//init dc dectet irq & delay work
	if (pdata->dc_det_pin != INVALID_GPIO){
		INIT_WORK(&data->dcwakeup_work, rk30_adc_battery_dcdet_delaywork);
		
		irq = gpio_to_irq(pdata->dc_det_pin);	        
		irq_flag = gpio_get_value (pdata->dc_det_pin) ? IRQF_TRIGGER_FALLING : IRQF_TRIGGER_RISING;
	    	ret = request_irq(irq, rk30_adc_battery_dc_wakeup, irq_flag, "ac_charge_irq", NULL);
	    	if (ret) {
	    		printk("failed to request dc det irq\n");
	    		goto err_dcirq_failed;
	    	}
	    	enable_irq_wake(irq);  
    	
	}
#endif

#if 0
	// batt low irq lowerpower_work
	if( pdata->batt_low_pin != INVALID_GPIO){
		INIT_WORK(&data->lowerpower_work, rk30_adc_battery_lowerpower_delaywork);
		
		irq = gpio_to_irq(pdata->batt_low_pin);
	    	ret = request_irq(irq, rk30_adc_battery_low_wakeup, IRQF_TRIGGER_LOW, "batt_low_irq", NULL);

	    	if (ret) {
	    		printk("failed to request batt_low_irq irq\n");
	    		goto err_lowpowerirq_failed;
	    	}
		disable_irq(irq);
    	
    	}
#endif
	//Power on Battery detect
	rk30_adc_battery_check(data);

	printk(KERN_INFO "rk30_adc_battery: driver initialized\n");
	
	return 0;
	
#if defined (CONFIG_BATTERY_RK30_USB_CHARGE)
err_usb_failed:
	power_supply_unregister(&rk30_usb_supply);
#endif

err_ac_failed:
#if defined (CONFIG_BATTERY_RK30_AC_CHARGE)
	power_supply_unregister(&rk30_ac_supply);
#endif

err_battery_failed:
	power_supply_unregister(&rk30_battery_supply);
    
err_dcirq_failed:
	free_irq(gpio_to_irq(pdata->dc_det_pin), data);
#if 1
 err_lowpowerirq_failed:
	free_irq(gpio_to_irq(pdata->batt_low_pin), data);
#endif
err_adc_register_failed:
err_io_init:    
err_data_alloc_failed:
	kfree(data);

	printk("rk30_adc_battery: error!\n");
    
	return ret;
}

static int rk30_adc_battery_remove(struct platform_device *pdev)
{
	struct rk30_adc_battery_data *data = platform_get_drvdata(pdev);
	struct rk30_adc_battery_platform_data *pdata = pdev->dev.platform_data;

	cancel_delayed_work(&gBatteryData->delay_work);	
#if defined(CONFIG_BATTERY_RK30_USB_CHARGE)
	power_supply_unregister(&rk30_usb_supply);
#endif
#if defined(CONFIG_BATTERY_RK30_AC_CHARGE)
	power_supply_unregister(&rk30_ac_supply);
#endif
	power_supply_unregister(&rk30_battery_supply);

	free_irq(gpio_to_irq(pdata->dc_det_pin), data);

	kfree(data);
	
	return 0;
}

static struct platform_driver rk30_adc_battery_driver = {
	.probe		= rk30_adc_battery_probe,
	.remove		= rk30_adc_battery_remove,
	.suspend		= rk30_adc_battery_suspend,
	.resume		= rk30_adc_battery_resume,
	.driver = {
		.name = "rk30-battery",
		.owner	= THIS_MODULE,
	}
};

static int __init rk30_adc_battery_init(void)
{
	return platform_driver_register(&rk30_adc_battery_driver);
}

static void __exit rk30_adc_battery_exit(void)
{
	platform_driver_unregister(&rk30_adc_battery_driver);
}

fs_initcall(rk30_adc_battery_init);//subsys_initcall(rk30_adc_battery_init);
module_exit(rk30_adc_battery_exit);

MODULE_DESCRIPTION("Battery detect driver for the rk30");
MODULE_AUTHOR("luowei lw@rock-chips.com");
MODULE_LICENSE("GPL");
