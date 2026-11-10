// oplus_bcc_boost.c —— 抬高 oplus 充电驱动的 BCC 电流上限
// 思路：kretprobe 挂所有 "取 BCC 上限" 的函数，把返回值乘 boost_pct%；
//       kprobe 挂 "下发 BCC 电流" 的函数，把入参电流乘 boost_pct%。
// 适配：一加13 PJZ110 / ColorOS 17 / kernel 6.6.118-android15
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kprobes.h>
#include <linux/version.h>

static int boost_pct = 150;
module_param(boost_pct, int, 0644);
MODULE_PARM_DESC(boost_pct, "百分比放大系数，150 = 上限提升 50%");

static int min_ua = 3000000;
module_param(min_ua, int, 0644);
MODULE_PARM_DESC(min_ua, "低于该值(µA)不做放大，避免误伤小电流路径");

/* ---------- 返回值放大（取上限类函数） ---------- */
static int ret_handler(struct kretprobe_instance *ri, struct pt_regs *regs)
{
    long *ret = (long *)&regs->regs[0];
    long v = *ret;

    if (v >= min_ua && v < 100000000L) {
        long nv = v * boost_pct / 100;
        pr_info("oplus_bcc_boost: ret %ld -> %ld\n", v, nv);
        *ret = nv;
    }
    return 0;
}

static char *ret_syms[] = {
    "oplus_chg_vphy_get_bcc_max_curr",
    "oplus_voocphy_get_bcc_max_curr",
    "oplus_voocphy_get_bcc_max_current",
    "oplus_adsp_voocphy_get_bcc_max_curr",
    "oplus_adsp_voocphy_get_bcc_max_current",
    "oplus_chg_vphy_get_bcc_max_current",
};
#define N_RET (sizeof(ret_syms)/sizeof(ret_syms[0]))
static struct kretprobe *krp;

/* ---------- 入参放大（下发电流类函数） ---------- */
static int entry_handler(struct kprobe *p, struct pt_regs *regs)
{
    /* arm64: x0=this, x1.. 依次为参数；逐个参数扫一遍找电流量级 */
    unsigned int i;
    for (i = 0; i < 6; i++) {
        long *r = (long *)&regs->regs[i];
        long v = *r;
        if (v >= min_ua && v < 100000000L) {
            long nv = v * boost_pct / 100;
            pr_info("oplus_bcc_boost: arg%d %ld -> %ld\n", i, v, nv);
            *r = nv;
        }
    }
    return 0;
}

static char *arg_syms[] = {
    "oplus_voocphy_set_bcc_current",
    "oplus_vphy_set_bcc_curr",
    "oplus_chg_vphy_set_bcc_curr",
    "oplus_chg_set_curr_level_to_voocphy",
    "voocphy_set_fcl",
};
#define N_ARG (sizeof(arg_syms)/sizeof(arg_syms[0]))
static struct kprobe *kp;

static int __init boost_init(void)
{
    unsigned int i, ok_r = 0, ok_a = 0;

    pr_info("oplus_bcc_boost: init boost_pct=%d\n", boost_pct);

    krp = kzalloc(sizeof(*krp) * N_RET, GFP_KERNEL);
    if (krp) {
        for (i = 0; i < N_RET; i++) {
            krp[i].kp.symbol_name = ret_syms[i];
            krp[i].entry_handler = ret_handler;
            krp[i].maxactive = 8;
            if (register_kretprobe(&krp[i]) == 0) {
                ok_r++;
                pr_info("oplus_bcc_boost: ret hook %s OK\n", ret_syms[i]);
            }
        }
    }

    kp = kzalloc(sizeof(*kp) * N_ARG, GFP_KERNEL);
    if (kp) {
        for (i = 0; i < N_ARG; i++) {
            kp[i].symbol_name = arg_syms[i];
            kp[i].pre_handler = entry_handler;
            if (register_kprobe(&kp[i]) == 0) {
                ok_a++;
                pr_info("oplus_bcc_boost: arg hook %s OK\n", arg_syms[i]);
            }
        }
    }

    pr_info("oplus_bcc_boost: hooked ret=%u/%u arg=%u/%u\n",
            ok_r, (unsigned)N_RET, ok_a, (unsigned)N_ARG);
    return 0;
}

static void __exit boost_exit(void)
{
    unsigned int i;
    if (krp) { for (i = 0; i < N_RET; i++) unregister_kretprobe(&krp[i]); kfree(krp); }
    if (kp)  { for (i = 0; i < N_ARG; i++) unregister_kprobe(&kp[i]);   kfree(kp); }
    pr_info("oplus_bcc_boost: exit\n");
}

module_init(boost_init);
module_exit(boost_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("oplus BCC current boost (PJZ110)");
