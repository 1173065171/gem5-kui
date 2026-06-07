# KuiLoong SAU Reference Sources

This directory stores source material extracted from the KuiLoong NPU
repositories for use as references while developing the gem5 `KuiSau`
model. These files are not included by `src/sau/SConscript`.

## Source Repositories

- Hardware: `/home/zbn/code/npu_lpnpu`
- Hardware filelist used as the extraction authority:
  `/home/zbn/code/npu_lpnpu/syn/script/mikui_dma/lpnpu_mikui_dma.f`
- Software simulator:
  `/home/zbn/code/toolchain_workdir/workdir/gitee_toolchain/kuiloong-sim/src/SAU`

## Layout

- `hardware/`: SAU RTL and direct support files.
- `hardware/lpnpu_mikui_dma_sau.f`: reduced SAU filelist with paths relative
  to this directory.
- `simulator/SAU/`: Python SAU simulator copied from `kuiloong-sim`.

## Notes

The hardware filelist in the source repository explicitly lists the SAU files
under `hardware/src/sa_execute` and `hardware/src/sa_element`, plus
`macro.svh` and `SA_pkg.sv`. This copy also includes:

- `hardware/macro/macro_gclk.svh`, because `SA_CORE.sv` includes it directly.
- `hardware/ip/GCLK/v_clock_latch.v` and `hardware/ip/GCLK/v_clock_pass.v`,
  because `macro_gclk.svh` maps `L_BIU_CG` to one of those clock-gate modules.

Python cache files from the simulator source were intentionally omitted.
