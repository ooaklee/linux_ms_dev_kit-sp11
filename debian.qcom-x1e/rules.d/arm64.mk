build_arch	= arm64
defconfig	= defconfig
flavours	= qcom-x1e
build_image	= vmlinuz.efi
kernel_file	= arch/$(build_arch)/boot/vmlinuz.efi
install_file	= vmlinuz
no_dumpfile	= true

vdso		= vdso_install

# Keep the annotated LLD linker while retaining the GCC compiler selection.
kmake += LD=ld.lld-21
# Pair LLVM binutils with the LLD link: GNU objcopy mangles empty lld
# sections (sh_entsize=0 on .relr.dyn), so its two-pass vmlinux strip in
# scripts/Makefile.vmlinux fails with "file format not recognized" on the
# second in-place pass.
kmake += OBJCOPY=llvm-objcopy-21

do_tools_usbip  = true
do_tools_cpupower = true
do_tools_perf   = true
do_tools_perf_jvmti = true
do_tools_perf_python = true
do_tools_bpftool = true
do_tools_rtla = true

do_dtbs		= true
do_stubble	= true
