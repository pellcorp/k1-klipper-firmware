// Report on user interface buttons
//
// Copyright (C) 2018  Kevin O'Connor <kevin@koconnor.net>
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include <math.h>
#include <stddef.h> // size_t
#include <stdint.h> // uint8_t
#include <stdlib.h>
#include <string.h> // memset

#include "board/gpio.h"     // struct gpio_in
#include "board/irq.h"      // irq_disable
#include "board/misc.h"     // timer_from_us
#include "board/internal.h" // gpio_peripheral
#include "command.h"        // DECL_COMMAND
#include "sched.h"          // struct timer
#include "stepper.h"

#define PR_VERSION (307)

#define MAX_BUF_LEN 32

struct pr_fifo
{
    int32_t  index;
    uint32_t buf[MAX_BUF_LEN];
};

void pr_fifo_write(struct pr_fifo *pf, uint32_t data)
{
    pf->buf[pf->index % MAX_BUF_LEN] = data;
    pf->index++;
}

void pr_fifo_read(struct pr_fifo *pf, uint32_t* out_buf)
{
    for(int i = 0; i < MAX_BUF_LEN; i++)
        out_buf[i] = pf->buf[(i + pf->index) % MAX_BUF_LEN];
}


struct pr_sys_time_cfg
{
    uint32_t is_run;
    struct timer time;
    double time_s;
    float  sys_time_duty;
};


struct pr_sys_time_cfg sys_time_cfg = {.is_run=0, .time_s=0, .sys_time_duty=0.001f};

static uint_fast8_t sys_time_event(struct timer *t)
{
    sys_time_cfg.time.waketime = timer_read_time() + (uint32_t)(sys_time_cfg.sys_time_duty * CONFIG_CLOCK_FREQ);
    sys_time_cfg.time_s += sys_time_cfg.sys_time_duty;
    return SF_RESCHEDULE;
}

void start_sys_time(void)
{
    if(sys_time_cfg.is_run != 0)
        return;
    sys_time_cfg.is_run = 1;
    sys_time_cfg.time.func = sys_time_event;
    sys_time_cfg.time.waketime = timer_read_time() + (uint32_t)(sys_time_cfg.sys_time_duty * CONFIG_CLOCK_FREQ);
    sched_add_timer(&sys_time_cfg.time);      
}

void stop_sys_time(void)
{
    if(sys_time_cfg.is_run != 1)
        return;
    sched_del_timer(&sys_time_cfg.time);
    sys_time_cfg.is_run = 0;
}

double get_sys_tick_s(void)
{
    return sys_time_cfg.time_s;
}


struct pr_swap_cfg
{   
    struct gpio_out out_pin;
    struct gpio_in in_pin;
};

struct pr_swap_cfg swap_cfg = {};

void write_swap_sta(int32_t sta)
{
    gpio_out_write(swap_cfg.out_pin, sta);
}

int32_t read_swap_sta(void)
{
    return gpio_in_read(swap_cfg.in_pin);
}


void send_debug_args(uint32_t *args)
{
    sendf("debug_prtouch oid=%c version=%u arg[0]=%u arg[1]=%u arg[2]=%u arg[3]=%u arg[4]=%u arg[5]=%u",
          (uint8_t)args[0], PR_VERSION, (uint32_t)args[0], (uint32_t)args[1], (uint32_t)args[2], (uint32_t)args[3], (uint32_t)args[4], (uint32_t)args[5]);
}


struct asy_delay
{
    double last_tick_s;
    double now_tick_s;
};

struct asy_delay send_dly = {.last_tick_s = 0, .now_tick_s = 0};

int32_t check_delay(struct asy_delay *ad, float s)
{
    ad->now_tick_s = get_sys_tick_s();
    if (ad->now_tick_s - ad->last_tick_s < s)
        return 0;
    ad->last_tick_s = ad->now_tick_s;
    return 1;
}


#define MAX_STEP_CNT 4
#define SIG_ARY_LEN 256

const uint16_t sigmoid_ary[SIG_ARY_LEN] = {
39, 40, 41, 42, 43, 44, 45, 46, 47, 49, 50, 51, 52, 53, 55, 56, 57, 59, 60, 62, 
63, 64, 66, 68, 69, 71, 72, 74, 76, 78, 79, 81, 83, 85, 87, 89, 91, 93, 95, 98, 
100, 102, 104, 107, 109, 112, 114, 117, 119, 122, 125, 127, 130, 133, 136, 139, 
142, 145, 148, 151, 154, 158, 161, 164, 168, 171, 175, 179, 182, 186, 190, 194, 
198, 202, 206, 210, 214, 218, 223, 227, 231, 236, 240, 245, 250, 254, 259, 264, 
269, 274, 279, 284, 289, 294, 299, 305, 310, 315, 321, 326, 332, 337, 343, 348, 
354, 360, 366, 371, 377, 383, 389, 395, 401, 407, 413, 419, 425, 431, 438, 444, 
450, 456, 462, 469, 475, 481, 487, 494, 500, 506, 512, 518, 525, 531, 537, 543, 
550, 556, 562, 568, 574, 580, 586, 592, 598, 604, 610, 616, 622, 628, 634, 640, 
645, 651, 657, 662, 668, 673, 679, 684, 690, 695, 700, 706, 711, 716, 721, 726, 
731, 736, 741, 745, 750, 755, 759, 764, 768, 773, 777, 781, 786, 790, 794, 798, 
802, 806, 810, 814, 817, 821, 825, 828, 832, 835, 839, 842, 845, 849, 852, 855, 
858, 861, 864, 867, 870, 873, 875, 878, 881, 883, 886, 888, 891, 893, 896, 898, 
900, 902, 905, 907, 909, 911, 913, 915, 917, 919, 920, 922, 924, 926, 927, 929, 
931, 932, 934, 935, 937, 938, 940, 941, 943, 944, 945, 947, 948, 949, 950, 951, 
952, 954, 955, 956, 957, 958, 959, 960};

struct pr_step_cfg
{
    uint16_t oid;
    struct timer time;
    struct asy_delay dly;
    //Step Cfg
    uint16_t cnt;
    uint16_t old_dirs[4];
    uint16_t dir_inverts[MAX_STEP_CNT];
    uint16_t step_invrets[MAX_STEP_CNT];
    struct gpio_out dirs[MAX_STEP_CNT];
    struct gpio_out steps[MAX_STEP_CNT];
    struct gpio_in in_dirs[MAX_STEP_CNT];
    //Send Cfg
    uint32_t send_tri_time;
    uint16_t send_active;
    uint16_t send_ms;
    struct   pr_fifo fifo_step;
    struct   pr_fifo fifo_tick;
    //Run Cfg
    uint16_t run_dir;    
    uint16_t need_stop;
    int32_t fix_steps;
    int32_t now_steps;
    int32_t half_ticks;   
    int32_t acc_ctl_cnt;
    uint16_t low_spd_nul;
    uint16_t send_step_duty;
    uint16_t auto_rtn;
};

struct pr_step_cfg step_cfg = {.send_active = 0};

void deal_dirs_prtouch(int dir, int is_save)
{
    for (int i = 0; i < step_cfg.cnt; i++)
    {
        step_cfg.old_dirs[i] = is_save > 0 ? gpio_in_read(step_cfg.in_dirs[i]) : step_cfg.old_dirs[i];
        gpio_out_write(step_cfg.dirs[i], is_save > 0 ? (dir != step_cfg.dir_inverts[i]) : step_cfg.old_dirs[i]);
    }
}

void deal_steps_prtouch(void)
{
    for (int i = 0; i < step_cfg.cnt; i++)
        gpio_out_toggle_noirq(step_cfg.steps[i]);
}

static uint_fast8_t prtouch_event(struct timer *t)
{
    deal_steps_prtouch();
    step_cfg.now_steps--;

    if(step_cfg.send_tri_time == 0 && read_swap_sta() == 1)
        step_cfg.send_tri_time = (uint32_t)(get_sys_tick_s() * 10000);

    if ((step_cfg.now_steps == 0) || ((step_cfg.now_steps % 2 == 0) && (step_cfg.need_stop == 1 || read_swap_sta() == 1)))
    {
        pr_fifo_write(&step_cfg.fifo_tick, (uint32_t)(get_sys_tick_s() * 10000));
        pr_fifo_write(&step_cfg.fifo_step, step_cfg.now_steps / 2);        
        if(step_cfg.auto_rtn == 1)
        {
            step_cfg.auto_rtn = 0;
            step_cfg.now_steps = step_cfg.fix_steps;
            deal_dirs_prtouch(0, 0);
            deal_dirs_prtouch(!step_cfg.run_dir, 1);
            step_cfg.time.waketime = timer_read_time() + step_cfg.half_ticks;
        }else
        {
            sched_del_timer(&step_cfg.time);
            deal_dirs_prtouch(0, 0);   
            step_cfg.send_active = MAX_BUF_LEN / 4;    
            step_cfg.need_stop = 0;
            step_cfg.now_steps = 0;
            return SF_RESCHEDULE; 
        }
    }else if(step_cfg.now_steps % (step_cfg.send_step_duty * 2) == 0)
    {
        pr_fifo_write(&step_cfg.fifo_tick, (uint32_t)(get_sys_tick_s() * 10000));
        pr_fifo_write(&step_cfg.fifo_step, step_cfg.now_steps / 2);
    }

    if(step_cfg.fix_steps - step_cfg.now_steps < step_cfg.acc_ctl_cnt)
    {
        int32_t index = SIG_ARY_LEN - (int32_t)((step_cfg.fix_steps - step_cfg.now_steps) * SIG_ARY_LEN / step_cfg.acc_ctl_cnt);
        index = index >= SIG_ARY_LEN ? (SIG_ARY_LEN - 1) : index;
        index = index < 0 ? 0 : index;
        step_cfg.time.waketime = timer_read_time() + step_cfg.half_ticks + step_cfg.low_spd_nul * step_cfg.half_ticks * sigmoid_ary[index] / 1000;
    }else if(step_cfg.now_steps < step_cfg.acc_ctl_cnt)
    {
        int32_t index = SIG_ARY_LEN - (int32_t)(step_cfg.now_steps * SIG_ARY_LEN / step_cfg.acc_ctl_cnt);
        index = index >= SIG_ARY_LEN ? (SIG_ARY_LEN - 1) : index;
        index = index < 0 ? 0 : index;
        step_cfg.time.waketime = timer_read_time() + step_cfg.half_ticks + step_cfg.low_spd_nul * step_cfg.half_ticks * sigmoid_ary[index] / 1000;   
    }
    else
        step_cfg.time.waketime = timer_read_time() + step_cfg.half_ticks;
    return SF_RESCHEDULE;
}

DECL_COMMAND(command_config_step_prtouch, "config_step_prtouch oid=%c step_cnt=%c swap_pin=%u sys_time_duty=%u");
void command_config_step_prtouch(uint32_t *args)
{
    step_cfg.oid = args[0];
    step_cfg.cnt = args[1];
    sys_time_cfg.sys_time_duty = (float)args[3] / 100000;
    swap_cfg.in_pin = gpio_in_setup(args[2], 1);
    send_debug_args(args);    
}

DECL_COMMAND(command_add_step_prtouch, "add_step_prtouch oid=%c index=%c dir_pin=%u step_pin=%u dir_invert=%c step_invert=%c");
void command_add_step_prtouch(uint32_t *args)
{
    step_cfg.in_dirs[args[1]] = gpio_in_setup(args[2], 0);
    step_cfg.dirs[args[1]] = gpio_out_setup(args[2], 0);
    step_cfg.steps[args[1]] = gpio_out_setup(args[3], args[5]);
    step_cfg.dir_inverts[args[1]] = args[4];
    step_cfg.step_invrets[args[1]] = args[5];
    send_debug_args(args);
}

DECL_COMMAND(command_read_swap_prtouch, "read_swap_prtouch oid=%c");
void command_read_swap_prtouch(uint32_t *args)
{
    sendf("result_read_swap_prtouch oid=%c sta=%c", step_cfg.oid, (char)read_swap_sta());
    send_debug_args(args);
}

DECL_COMMAND(command_start_step_prtouch, "start_step_prtouch oid=%c dir=%c send_ms=%c step_cnt=%u step_us=%u acc_ctl_cnt=%u low_spd_nul=%c send_step_duty=%c auto_rtn=%c");
void command_start_step_prtouch(uint32_t *args)
{
    if (args[2] == 0)
    {
        step_cfg.need_stop = 1;
        send_debug_args(args);
        stop_sys_time();
        return;
    }
    start_sys_time();

    step_cfg.fifo_tick.index = 0;
    step_cfg.fifo_step.index = 0;
    memset(&step_cfg.fifo_tick.buf, 0, sizeof(step_cfg.fifo_tick.buf));
    memset(&step_cfg.fifo_step.buf, 0, sizeof(step_cfg.fifo_step.buf));
    for(int i = 0; i < MAX_BUF_LEN; i++)
        step_cfg.fifo_tick.buf[i] = (uint32_t)(get_sys_tick_s() * 10000);

    deal_dirs_prtouch(args[1], 1);
    step_cfg.run_dir = args[1];    
    step_cfg.send_ms = args[2];
    step_cfg.fix_steps = args[3] * 2;
    step_cfg.now_steps = step_cfg.fix_steps;    
    step_cfg.half_ticks = (uint32_t)(((double)args[4] / 1000000) * CONFIG_CLOCK_FREQ) / 2;
    step_cfg.acc_ctl_cnt = args[5] * 2;
    step_cfg.send_active = 0;
    step_cfg.send_tri_time = 0;
    step_cfg.low_spd_nul = args[6];
    step_cfg.send_step_duty = args[7];
    step_cfg.auto_rtn = args[8];

    step_cfg.time.func = prtouch_event;
    step_cfg.time.waketime = timer_read_time() + step_cfg.half_ticks * (1 + step_cfg.low_spd_nul);
    step_cfg.need_stop = 0;
    sched_add_timer(&step_cfg.time);
    send_debug_args(args);
}

DECL_COMMAND(command_manual_get_steps, "manual_get_steps oid=%c index=%c");
void command_manual_get_steps(uint32_t *args)
{
    int i = args[1];
    sendf("result_manual_get_steps oid=%c index=%c tri_time=%u tick0=%u tick1=%u tick2=%u tick3=%u step0=%u step1=%u step2=%u step3=%u",
        (uint8_t)step_cfg.oid, i, step_cfg.send_tri_time,
        step_cfg.fifo_tick.buf[i + 0], step_cfg.fifo_tick.buf[i + 1], step_cfg.fifo_tick.buf[i + 2], step_cfg.fifo_tick.buf[i + 3],
        step_cfg.fifo_step.buf[i + 0], step_cfg.fifo_step.buf[i + 1], step_cfg.fifo_step.buf[i + 2], step_cfg.fifo_step.buf[i + 3]);
}

void prtouch_step_task(void)
{
    if(step_cfg.send_active == 0)
        return;
    if (!check_delay(&send_dly, (float)step_cfg.send_ms / 1000))
        return;

    if(step_cfg.fifo_tick.index != 0)
    {
        uint32_t tmp_buf[MAX_BUF_LEN] = {0};

        pr_fifo_read(&step_cfg.fifo_tick, tmp_buf);
        for(int i = 0; i < MAX_BUF_LEN; i++)
            step_cfg.fifo_tick.buf[i] = tmp_buf[i];
        step_cfg.fifo_tick.index = 0;

        pr_fifo_read(&step_cfg.fifo_step, tmp_buf);
        for(int i = 0; i < MAX_BUF_LEN; i++)
            step_cfg.fifo_step.buf[i] = tmp_buf[i];
        step_cfg.fifo_step.index = 0;     
    }

    step_cfg.send_active--;
    int i = (7 - step_cfg.send_active) * 4;
    sendf("result_run_step_prtouch oid=%c index=%c tri_time=%u tick0=%u tick1=%u tick2=%u tick3=%u step0=%u step1=%u step2=%u step3=%u",
        (uint8_t)step_cfg.oid, i, step_cfg.send_tri_time,
        step_cfg.fifo_tick.buf[i + 0], step_cfg.fifo_tick.buf[i + 1], step_cfg.fifo_tick.buf[i + 2], step_cfg.fifo_tick.buf[i + 3],
        step_cfg.fifo_step.buf[i + 0], step_cfg.fifo_step.buf[i + 1], step_cfg.fifo_step.buf[i + 2], step_cfg.fifo_step.buf[i + 3]);
}


#define MAX_PRES_CNT 4

struct pr_pres_cfg
{
    uint16_t oid;
    struct asy_delay dly;
    //Pres Cfg
    uint16_t cnt;
    uint16_t use_adc;
    struct gpio_adc adcs[MAX_PRES_CNT];
    struct gpio_out clks[MAX_PRES_CNT];
    struct gpio_in  sdos[MAX_PRES_CNT];
    //Buff Cfg
    struct   pr_fifo fifo_tick;    
    struct   pr_fifo fifo_pres[MAX_PRES_CNT];
    int32_t  send_active;
    uint32_t send_tri_time;
    //Read Cfg
    uint16_t read_acq_ms;
    uint16_t read_now_cnt;
    uint16_t read_fix_cnt;
    //Tri Cfg
    uint16_t tri_chs;
    uint16_t tri_run_dir;
    uint32_t tri_acq_cnt;
    uint16_t tri_send_ms;
    uint16_t tri_acq_ms;
    float    tri_hftr_cut;
    float    tri_lftr_k1;  
    uint16_t tri_need_cnt;    
    uint32_t tri_min_hold;    
    uint32_t tri_max_hold;
    uint32_t buf_cnt;
    int32_t  tri_need_stop;
    int32_t  tri_avg_vals[MAX_PRES_CNT];
};

struct pr_pres_cfg pres_cfg = {.tri_acq_ms = 0, .send_active = 0};

int32_t read_pres_prtouch(int32_t *out_buf)
{
    if (pres_cfg.use_adc)
    {
        for (int i = 0; i < pres_cfg.cnt; i++)
        {
            double last_s = get_sys_tick_s();
            while (0 != gpio_adc_sample(pres_cfg.adcs[i]) && (get_sys_tick_s() - last_s < 0.002))
                ;
            out_buf[i] = gpio_adc_read(pres_cfg.adcs[i]);
        }
        return 1;
    }
    else
    {
        static double last_tick_s = 0;
        int32_t is_data_valid = 0;
        for (int j = 0; j < pres_cfg.cnt; j++)
            is_data_valid |= (gpio_in_read(pres_cfg.sdos[j]) << j);

        if (is_data_valid == 0 || (get_sys_tick_s() - last_tick_s >= 0.025))
        {
            for (int j = 0; j < pres_cfg.cnt; j++)
                gpio_out_write(pres_cfg.clks[j], 0);

            for (int i = 0; i < 24; i++)
            {
                for (int j = 0; j < pres_cfg.cnt; j++)
                    gpio_out_write(pres_cfg.clks[j], 1);
                for (int j = 0; j < pres_cfg.cnt; j++)
                    out_buf[j] = out_buf[j] << 1;
                for (int j = 0; j < pres_cfg.cnt; j++)
                    gpio_out_write(pres_cfg.clks[j], 0);
                for (int j = 0; j < pres_cfg.cnt; j++)
                    out_buf[j] += (gpio_in_read(pres_cfg.sdos[j]) > 0 ? 1 : 0);
            }
            for (int j = 0; j < pres_cfg.cnt; j++)
                gpio_out_write(pres_cfg.clks[j], 1);
            for (int j = 0; j < pres_cfg.cnt; j++)
            {
                out_buf[j] |= ((out_buf[j] & 0x00800000) != 0 ? 0xFF000000 : 0);
            }
            for (int j = 0; j < pres_cfg.cnt; j++)
                gpio_out_write(pres_cfg.clks[j], 0);

            last_tick_s = get_sys_tick_s();

            for (int i = 0; i < pres_cfg.cnt; i++)
                out_buf[i] -= pres_cfg.tri_avg_vals[i];
            return 1;
        }
    }
    return 0;
}

double* filter_datas_prtouch(int32_t ch, int32_t is_cover_src)
{
    static int32_t buf_cpy[MAX_BUF_LEN] = {0};
    static double buf_tmp[MAX_BUF_LEN] = {0};

    memset(buf_cpy, 0, sizeof(buf_cpy));
    memset(buf_tmp, 0, sizeof(buf_tmp));

    // 0. Copy 
    pr_fifo_read(&pres_cfg.fifo_pres[ch], (uint32_t*)buf_cpy);
    if(buf_cpy[MAX_BUF_LEN - 1] < buf_cpy[0])
    {
        for (int j = 0; j < MAX_BUF_LEN; j++)
            buf_cpy[j] = pres_cfg.use_adc ? (4096 - buf_cpy[j]) : (-buf_cpy[j]);
    } 

    if(pres_cfg.use_adc)
    {
        if(!is_cover_src)
        {
            for(int i = 0; i < MAX_BUF_LEN; i++)
                buf_tmp[i] = buf_cpy[i];   

            // 3. L Filter
            for (int j = 1; j < MAX_BUF_LEN; j++)
                buf_tmp[j] = buf_tmp[j] * pres_cfg.tri_lftr_k1 + buf_tmp[j - 1] * (1.0f - pres_cfg.tri_lftr_k1);                 
        }else
        {
            pres_cfg.fifo_pres[ch].index = 0;            
            for(int i = 0; i < MAX_BUF_LEN; i++)
                pres_cfg.fifo_pres[ch].buf[i] = (uint32_t)buf_cpy[i];

            if(ch == pres_cfg.cnt - 1)
            {
                pr_fifo_read(&pres_cfg.fifo_tick, (uint32_t*)buf_cpy);
                pres_cfg.fifo_tick.index = 0;                    
                for(int i = 0; i < MAX_BUF_LEN; i++)
                    pres_cfg.fifo_tick.buf[i] = (uint32_t)buf_cpy[i];
            }
        }       
        return buf_tmp;
    }else
    {
        if(!is_cover_src)
        {
            // 1. T Filter
            for (int j = 0; j < MAX_BUF_LEN - 2; j++)
            {
                double minVal = (fabs(buf_cpy[j]) < fabs(buf_cpy[j + 1]) ? buf_cpy[j] : buf_cpy[j + 1]);
                buf_cpy[j] = fabs(minVal) < fabs(buf_cpy[j + 2]) ? minVal : buf_cpy[j + 2];
            }

            // 2. H Filter
            double rc = 1.0f / 2.0f / 3.1415926f / pres_cfg.tri_hftr_cut;
            double coff = rc / (rc + 1.0f / (1000.0f / pres_cfg.tri_acq_ms));
            for (int i = 1; i < MAX_BUF_LEN; i++)
                buf_tmp[i] = (buf_cpy[i] - buf_cpy[i - 1] + buf_tmp[i - 1]) * coff;

            // 3. L Filter
            for (int j = 1; j < MAX_BUF_LEN; j++)
                buf_tmp[j] = buf_tmp[j] * pres_cfg.tri_lftr_k1 + buf_tmp[j - 1] * (1.0f - pres_cfg.tri_lftr_k1);        
        }else 
        {
            for(int i = 0; i < MAX_BUF_LEN; i++)
                pres_cfg.fifo_pres[ch].buf[i] = buf_cpy[i];
            pres_cfg.fifo_pres[ch].index = 0;

            if(ch == pres_cfg.cnt - 1)
            {
                pres_cfg.buf_cnt = pres_cfg.fifo_tick.index;    
                pr_fifo_read(&pres_cfg.fifo_tick, (uint32_t*)buf_cpy);
                for(int i = 0; i < MAX_BUF_LEN; i++)
                    pres_cfg.fifo_tick.buf[i] = (uint32_t)buf_cpy[i];
                pres_cfg.fifo_tick.index = 0;   
            }
        }        
    }


    return buf_tmp;  
}

int32_t check_pres_tri_prtouch(void)
{
    int32_t tri_cnt = 0;

    if (pres_cfg.use_adc)
    {
        pres_cfg.tri_chs = 0;
        for (int i = 0; i < pres_cfg.cnt; i++)
        {
            double* p_buf = filter_datas_prtouch(i, 0);  

            int is_cnt = 0;
            for (int j = 0; j < pres_cfg.tri_min_hold; j++)
                is_cnt += (p_buf[MAX_BUF_LEN - 1 - j] >= pres_cfg.tri_max_hold ? 1 : 0);
            tri_cnt += (is_cnt == pres_cfg.tri_min_hold ? 1 : 0);
            pres_cfg.tri_chs |= (is_cnt == pres_cfg.tri_min_hold ? (0x01 << i) : 0);
        }
    }
    else
    {
        pres_cfg.tri_chs = 0;
        for (int i = 0; i < pres_cfg.cnt; i++)
        {
            double* p_buf = filter_datas_prtouch(i, 0);   
            // 1. Max Hold Check
            if ((p_buf[MAX_BUF_LEN - 1] > (pres_cfg.tri_max_hold / 1)) && (p_buf[MAX_BUF_LEN - 2] > (pres_cfg.tri_max_hold / 2)) && (p_buf[MAX_BUF_LEN - 3] > (pres_cfg.tri_max_hold / 3)))
            {
                tri_cnt++;
                pres_cfg.tri_chs |= (0x01 << i);
                continue;
            }            

            if(pres_cfg.fifo_pres[0].index < MAX_BUF_LEN)
                continue;
            // 2. a < b < c Check
            if (!(p_buf[MAX_BUF_LEN - 1] > p_buf[MAX_BUF_LEN - 2] && p_buf[MAX_BUF_LEN - 2] > p_buf[MAX_BUF_LEN - 3]))
                continue;
            // 3. Last 3 point must up of all other.
            int32_t end3_up_all_cnt = 0;
            for (int j = 0; j < MAX_BUF_LEN - 3; j++)
                end3_up_all_cnt += (p_buf[MAX_BUF_LEN - 1] > p_buf[j] && p_buf[MAX_BUF_LEN - 2] > p_buf[j] && p_buf[MAX_BUF_LEN - 3] > p_buf[j]) ? 1 : 0;
            if (end3_up_all_cnt != MAX_BUF_LEN - 3)
                continue;
            // 4. Min Hold Theck.
            if (p_buf[MAX_BUF_LEN - 1] < pres_cfg.tri_min_hold)
                continue;
            pres_cfg.tri_chs |= (0x01 << i);
            tri_cnt++;
        }        
    }

    return tri_cnt;
}

DECL_COMMAND(command_config_pres_prtouch, "config_pres_prtouch oid=%c use_adc=%c pres_cnt=%c swap_pin=%u sys_time_duty=%u");
void command_config_pres_prtouch(uint32_t *args)
{
    pres_cfg.oid = args[0];
    pres_cfg.use_adc = args[1];    
    pres_cfg.cnt = args[2];
    sys_time_cfg.sys_time_duty = (float)args[4] / 100000;
    swap_cfg.out_pin = gpio_out_setup(args[3], 0);
    write_swap_sta(0);
    send_debug_args(args);
}

DECL_COMMAND(command_add_pres_prtouch, "add_pres_prtouch oid=%c index=%c clk_pin=%u sda_pin=%u");
void command_add_pres_prtouch(uint32_t *args)
{
    if(pres_cfg.use_adc)
        pres_cfg.adcs[args[1]] = gpio_adc_setup(args[2]);
    else
    {
        pres_cfg.clks[args[1]] = gpio_out_setup(args[2], 0);
        pres_cfg.sdos[args[1]] = gpio_in_setup(args[3], 0);        
    }
    send_debug_args(args);
}

DECL_COMMAND(command_write_swap_prtouch, "write_swap_prtouch oid=%c sta=%c");
void command_write_swap_prtouch(uint32_t *args)
{
    write_swap_sta(args[1]);
    send_debug_args(args); 
    sendf("resault_write_swap_prtouch oid=%c", pres_cfg.oid);
}

DECL_COMMAND(command_read_pres_prtouch, "read_pres_prtouch oid=%c acq_ms=%u cnt=%u");
void command_read_pres_prtouch(uint32_t *args)
{
    if(args[2] == 0)
    {
        pres_cfg.read_fix_cnt = 0;
        stop_sys_time(); 
        return;
    }
    start_sys_time();
    pres_cfg.read_acq_ms = args[1];
    pres_cfg.read_fix_cnt = args[2];
    pres_cfg.read_now_cnt = 0;
    check_delay(&pres_cfg.dly, 0); 
    send_debug_args(args);
}

DECL_COMMAND(command_deal_avgs_prtouch, "deal_avgs_prtouch oid=%c base_cnt=%c");
void command_deal_avgs_prtouch(uint32_t *args)
{
    send_debug_args(args);  
    start_sys_time();
    memset(pres_cfg.tri_avg_vals, 0, sizeof(pres_cfg.tri_avg_vals));
    if (!pres_cfg.use_adc && args[1] != 0)
    {
        for(int i = 0; i < pres_cfg.cnt; i++)
        {
            pres_cfg.fifo_pres[i].index = 0;            
            memset(&pres_cfg.fifo_pres[i].buf, 0, sizeof(pres_cfg.fifo_pres[i].buf));            
        }

        int32_t rd_cnt = args[1];    
        rd_cnt = rd_cnt > MAX_BUF_LEN ? MAX_BUF_LEN : rd_cnt;
        rd_cnt = rd_cnt < 8 ? 8 : rd_cnt;

        for (int i = 0; i < rd_cnt; i++)
        {
            int32_t buf[MAX_PRES_CNT] = {0};
            while (read_pres_prtouch(buf) == 0)
                ;
            for(int j = 0; j < pres_cfg.cnt; j++)
                pr_fifo_write(&pres_cfg.fifo_pres[j], buf[j]);
        }

        for(int m = 0; m < pres_cfg.cnt; m++)
        {
            for(int i = 0; i < rd_cnt; i++)
            {
                for(int j = i; j < rd_cnt; j++)
                {
                    if(pres_cfg.fifo_pres[m].buf[i] > pres_cfg.fifo_pres[m].buf[j])
                    {
                        int32_t tmp = pres_cfg.fifo_pres[m].buf[i];
                        pres_cfg.fifo_pres[m].buf[i] = pres_cfg.fifo_pres[m].buf[j];
                        pres_cfg.fifo_pres[m].buf[j] = tmp;
                    }             
                }  
            }        
        }

        for(int i = 0; i < pres_cfg.cnt; i++)
        {
            for(int j = 2; j < rd_cnt - 2; j++)
                pres_cfg.tri_avg_vals[i] += pres_cfg.fifo_pres[i].buf[j];
            pres_cfg.tri_avg_vals[i] /= (rd_cnt - 4);
        }        
    }
    stop_sys_time();
    sendf("result_deal_avgs_prtouch oid=%c ch0=%i ch1=%i ch2=%i ch3=%i", pres_cfg.oid, pres_cfg.tri_avg_vals[0], pres_cfg.tri_avg_vals[1], pres_cfg.tri_avg_vals[2], pres_cfg.tri_avg_vals[3]);     
}

DECL_COMMAND(command_start_pres_prtouch, "start_pres_prtouch oid=%c tri_dir=%c acq_ms=%c send_ms=%c need_cnt=%c tri_hftr_cut=%u tri_lftr_k1=%u min_hold=%u max_hold=%u");
void command_start_pres_prtouch(uint32_t *args)
{
    send_debug_args(args);
    write_swap_sta(0);    
    if(args[4] == 0)
    {
        pres_cfg.tri_acq_ms = 0;
        stop_sys_time();
        return;
    }
    start_sys_time();
    pres_cfg.fifo_tick.index = (args[1] == 0 ? 0 : (MAX_BUF_LEN - 1));
    memset(&pres_cfg.fifo_tick.buf, 0, sizeof(pres_cfg.fifo_tick.buf));
    for(int i = 0; i < MAX_BUF_LEN; i++)
        pres_cfg.fifo_tick.buf[i] = (uint32_t)(get_sys_tick_s() * 10000);
    for(int i = 0; i < pres_cfg.cnt; i++)
    {
        pres_cfg.fifo_pres[i].index = (args[1] == 0 ? 0 : (MAX_BUF_LEN - 1));
        memset(&pres_cfg.fifo_pres[i].buf, 0, sizeof(pres_cfg.fifo_pres[i].buf));        
    }

    pres_cfg.tri_run_dir = args[1];    
    pres_cfg.tri_send_ms = args[3];
    pres_cfg.tri_need_cnt = args[4];
    pres_cfg.tri_hftr_cut = (float)args[5] / 1000.0f;
    pres_cfg.tri_lftr_k1 = (float)args[6] / 1000.0f;
    pres_cfg.tri_min_hold = args[7];
    pres_cfg.tri_max_hold = args[8];

    pres_cfg.send_tri_time = 0;
    pres_cfg.send_active = 0;
    pres_cfg.tri_acq_ms = args[2];
}

DECL_COMMAND(command_manual_get_pres, "manual_get_pres oid=%c index=%c");
void command_manual_get_pres(uint32_t *args)
{
    int i = args[1];
    sendf("resault_manual_get_pres oid=%c index=%c tri_time=%u tri_chs=%c buf_cnt=%u tick_0=%u ch0_0=%i ch1_0=%i ch2_0=%i ch3_0=%i tick_1=%u ch0_1=%i ch1_1=%i ch2_1=%i ch3_1=%i",
        (uint8_t)pres_cfg.oid, i, pres_cfg.send_tri_time, (uint8_t)pres_cfg.tri_chs, pres_cfg.buf_cnt,
        pres_cfg.fifo_tick.buf[i + 0], (int32_t)pres_cfg.fifo_pres[0].buf[i + 0], (int32_t)pres_cfg.fifo_pres[1].buf[i + 0], (int32_t)pres_cfg.fifo_pres[2].buf[i + 0], (int32_t)pres_cfg.fifo_pres[3].buf[i + 0],
        pres_cfg.fifo_tick.buf[i + 1], (int32_t)pres_cfg.fifo_pres[0].buf[i + 1], (int32_t)pres_cfg.fifo_pres[1].buf[i + 1], (int32_t)pres_cfg.fifo_pres[2].buf[i + 1], (int32_t)pres_cfg.fifo_pres[3].buf[i + 1]);
}

void prtouch_pres_task(void)
{
    int32_t vals[MAX_PRES_CNT] = {0};

    if (pres_cfg.tri_acq_ms != 0)
    {
        if ((pres_cfg.use_adc && !check_delay(&pres_cfg.dly, (float)pres_cfg.tri_acq_ms / 1000)) || !read_pres_prtouch(vals))
            return;

        pr_fifo_write(&pres_cfg.fifo_tick, (uint32_t)(get_sys_tick_s() * 10000));
        for (int i = 0; i < pres_cfg.cnt; i++)
            pr_fifo_write(&pres_cfg.fifo_pres[i], vals[i]);

        int is_tri = check_pres_tri_prtouch();
        if (is_tri < pres_cfg.tri_need_cnt || pres_cfg.tri_chs == 0)
            return;
        pres_cfg.tri_acq_ms = 0;
        pres_cfg.send_tri_time = (uint32_t)(get_sys_tick_s() * 10000);
        write_swap_sta(1);
        for (int i = 0; i < pres_cfg.cnt; i++) // filter all datas
            filter_datas_prtouch(i, 1);
        pres_cfg.send_active = MAX_BUF_LEN / 2;
    }
    else if (pres_cfg.read_fix_cnt != 0)
    {
        if ((pres_cfg.use_adc && !check_delay(&pres_cfg.dly, (float)pres_cfg.read_acq_ms / 1000)) || !read_pres_prtouch(vals))
            return;

        sendf("result_read_pres_prtouch oid=%c tick=%u ch0=%i ch1=%i ch2=%i ch3=%i", pres_cfg.oid, (uint32_t)(get_sys_tick_s() * 10000), vals[0], vals[1], vals[2], vals[3]);
        pres_cfg.read_now_cnt++;
        if (pres_cfg.read_now_cnt == pres_cfg.read_fix_cnt)
        {
            pres_cfg.read_fix_cnt = 0;   
            stop_sys_time(); 
        }
    }

    if(pres_cfg.send_active == 0)
        return;

    if (!check_delay(&send_dly, (float)pres_cfg.tri_send_ms / 1000))
        return; 
        
    pres_cfg.send_active--;
    int i = ((MAX_BUF_LEN / 2 - 1) - pres_cfg.send_active) * 2;
    sendf("result_run_pres_prtouch oid=%c index=%c tri_time=%u tri_chs=%c buf_cnt=%u tick_0=%u ch0_0=%i ch1_0=%i ch2_0=%i ch3_0=%i tick_1=%u ch0_1=%i ch1_1=%i ch2_1=%i ch3_1=%i",
        (uint8_t)pres_cfg.oid, i, pres_cfg.send_tri_time, (uint8_t)pres_cfg.tri_chs, pres_cfg.buf_cnt,
        pres_cfg.fifo_tick.buf[i + 0], (int32_t)pres_cfg.fifo_pres[0].buf[i + 0], (int32_t)pres_cfg.fifo_pres[1].buf[i + 0], (int32_t)pres_cfg.fifo_pres[2].buf[i + 0], (int32_t)pres_cfg.fifo_pres[3].buf[i + 0],
        pres_cfg.fifo_tick.buf[i + 1], (int32_t)pres_cfg.fifo_pres[0].buf[i + 1], (int32_t)pres_cfg.fifo_pres[1].buf[i + 1], (int32_t)pres_cfg.fifo_pres[2].buf[i + 1], (int32_t)pres_cfg.fifo_pres[3].buf[i + 1]);
}

void prtouch_task(void)
{
    prtouch_pres_task();    
    prtouch_step_task();
}
