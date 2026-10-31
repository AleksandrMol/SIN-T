// Привязка к старым версиям функций libm. Fedora 44 собирает с glibc 2.43, а среда
// flatpak-Reaper старше, и плагин с новыми версиями символов там не загрузится.
#if defined(__linux__) && defined(__x86_64__)
__asm__(".symver acosf,acosf@GLIBC_2.2.5");
__asm__(".symver asinf,asinf@GLIBC_2.2.5");
__asm__(".symver atan2f,atan2f@GLIBC_2.2.5");
__asm__(".symver sqrtf,sqrtf@GLIBC_2.2.5");
#endif
