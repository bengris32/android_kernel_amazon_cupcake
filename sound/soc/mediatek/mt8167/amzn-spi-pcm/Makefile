MTK_PLATFORM := $(subst ",,$(CONFIG_MTK_PLATFORM))
subdir-ccflags-y += -Werror -I$(srctree)/drivers/misc/mediatek/base/power/$(MTK_PLATFORM)

amzn-mt-spi-objs := amzn-mt-spi-pcm.o

obj-$(CONFIG_SND_SOC_MT8516_abc123_MACH)	+= amzn-mt-spi.o
