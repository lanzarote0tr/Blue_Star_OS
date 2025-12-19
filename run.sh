set -e
. edksetup.sh
build --platform=OvmfPkg/OvmfPkgX64.dsc --arch=X64 --buildtarget=RELEASE --tagname=GCC5

cp Build/OvmfX64/RELEASE_GCC5/X64/BlueStarOS.efi EFIfile/os.efi

qemu-system-x86_64 \
    -bios OVMF.fd \
    -drive if=pflash,format=raw,readonly=on,file=OVMF.fd \
    -drive file=fat:rw:EFIfile,format=raw \
    -net none \
    -m 512M \
    -monitor stdio \
    -d int -D log.txt
