#ifndef __GF_PLATFORM_H
#define __GF_PLATFORM_H
struct gf_device;
void gf_spi_clk_enable(struct gf_device *gf_dev);
void gf_spi_clk_disable(struct gf_device *gf_dev);
void gf_reg_screen_notify(struct gf_device *gf_dev);
void gf_unreg_screen_notify(struct gf_device *gf_dev);
void gf_reg_spi_driver(void);
void gf_un_reg_spi_driver(void);
void gf_power_on(struct gf_device *gf_dev);
void gf_power_off(struct gf_device *gf_dev);
int gf_hw_reset(struct gf_device *gf_dev, unsigned int delay_ms);

#define GF_POWER_PMIC_LDO//support GF_POWER_GPIO/GF_POWER_EXT_LDO/GF_POWER_PMIC_LDO

#if defined(GF_PLAT_MTK)
    #define GF_SUPPORT_PINCTRL//suppor pinctrl for cs/rst/vdd gpio control,disable it for use linux gpio function
#elif defined(GF_PLAT_QCOM)
    #define GF_SUPPORT_QCOM_DRM//for reg qcom drm panel screen on/off notify
#elif defined(GF_PLAT_SPRD)
    #define GF_SUPPORT_GPIOD//for sprd only support GPIOD
#endif

#endif	/* __GF_PLATFORM_H */
