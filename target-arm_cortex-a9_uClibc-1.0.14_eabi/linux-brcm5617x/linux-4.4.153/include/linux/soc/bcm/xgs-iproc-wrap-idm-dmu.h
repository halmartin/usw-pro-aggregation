/*
 * $Copyright Open Broadcom Corporation$
 */

#ifndef XGS_IPROC_WRAP_IDM_DMU_H
#define XGS_IPROC_WRAP_IDM_DMU_H

extern int xgs_iproc_wrap_idm_dmu_base_reg_setup(void);
extern void xgs_iproc_idm_timeout_handler_setup(void);
extern void __iomem *get_iproc_wrap_ctrl_base(void);
extern void __iomem *get_iproc_dmu_pcu_base(void);
extern void __iomem *get_iproc_idm_base(int index);
extern void __iomem *get_iproc_idm_base_phys(int index);

#endif
