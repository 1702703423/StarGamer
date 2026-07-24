#!/usr/bin/env python3
"""Validate the IRQ ordering required by STM32 HAL SD DMA transfers."""

from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[2]
IOC_PATH = ROOT / "Star Gamer.ioc"
MAIN_PATH = ROOT / "Core" / "Src" / "main.c"
MSP_PATH = ROOT / "Core" / "Src" / "stm32f4xx_hal_msp.c"


def ioc_priority(text: str, irq: str) -> int:
    match = re.search(rf"^NVIC\.{re.escape(irq)}=true\\:(\d+)\\:", text, re.MULTILINE)
    if match is None:
        raise AssertionError(f"missing {irq} in {IOC_PATH}")
    return int(match.group(1))


def source_priority(text: str, irq: str, path: Path) -> int:
    match = re.search(
        rf"HAL_NVIC_SetPriority\({re.escape(irq)},\s*(\d+)\s*,\s*0\s*\)",
        text,
    )
    if match is None:
        raise AssertionError(f"missing {irq} priority in {path}")
    return int(match.group(1))


def validate_order(rx_priority: int, tx_priority: int, sdio_priority: int) -> None:
    if not rx_priority < sdio_priority:
        raise AssertionError("SDIO RX DMA IRQ must preempt the SDIO IRQ")
    if not tx_priority < sdio_priority:
        raise AssertionError("SDIO TX DMA IRQ must preempt the SDIO IRQ")


def main() -> None:
    ioc = IOC_PATH.read_text(encoding="utf-8")
    main_source = MAIN_PATH.read_text(encoding="utf-8")
    msp_source = MSP_PATH.read_text(encoding="utf-8")

    ioc_priorities = (
        ioc_priority(ioc, "DMA2_Stream3_IRQn"),
        ioc_priority(ioc, "DMA2_Stream6_IRQn"),
        ioc_priority(ioc, "SDIO_IRQn"),
    )
    source_priorities = (
        source_priority(main_source, "DMA2_Stream3_IRQn", MAIN_PATH),
        source_priority(main_source, "DMA2_Stream6_IRQn", MAIN_PATH),
        source_priority(msp_source, "SDIO_IRQn", MSP_PATH),
    )

    validate_order(*ioc_priorities)
    validate_order(*source_priorities)
    if source_priorities != ioc_priorities:
        raise AssertionError(
            f"generated priorities {source_priorities} do not match IOC {ioc_priorities}"
        )

    print(
        "SDIO IRQ priority OK: "
        f"RX={source_priorities[0]} TX={source_priorities[1]} "
        f"SDIO={source_priorities[2]}"
    )


if __name__ == "__main__":
    main()
