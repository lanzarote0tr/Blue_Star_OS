set -e

ensure_base_tools() {
    # Build BaseTools C binaries if core tool (GenFw) is missing.
    if [ ! -x BaseTools/Source/C/bin/GenFw ]; then
        echo "Building BaseTools (missing GenFw)..."
        make -s -C BaseTools/Source/C >/dev/null
    fi
}

ensure_base_tools

. edksetup.sh
build --platform=OvmfPkg/OvmfPkgX64.dsc --arch=X64 --buildtarget=RELEASE --tagname=GCC5

cd BlueStarOS/kernel
make
cd ../..

cp Build/OvmfX64/RELEASE_GCC5/X64/BOOTX64.efi EFIfile/BOOTX64.efi
cp BlueStarOS/kernel/build/kernel EFIfile/kernel
cp BlueStarOS/kernel/build/kernel.elf EFIfile/kernel.elf
