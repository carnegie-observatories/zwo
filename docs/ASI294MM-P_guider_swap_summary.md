# Swope Guider Camera Replacement: Feasibility Summary

**Proposal:** Mount an **ASI294MM-P** on an existing Swope guider port for an on-sky test, **without modifying the relay optics**.

## TL;DR

- **No optical modifications required** for an initial test — the new camera mounts directly on the existing guider port.
- **Expected SNR gain: ~5–10%**, driven mostly by higher quantum efficiency. Modest, but consistently positive across all guide-star brightnesses.
- **No FOV penalty.** The illuminated patch from the existing optics (~2.78 arcmin) is well within the new sensor's active area.
- Other practical benefits — modern USB3 interface, faster cadence, reduced obsolescence risk — make the test attractive even if SNR alone is not the deciding factor.

## Current Guider (Baseline)

| Parameter | Value |
|---|---|
| Detector | E2V CCD47-20 (back-illuminated frame-transfer CCD) |
| Pixel size | 13 µm |
| Pixel scale on Swope | 0.163 arcsec/pix |
| Field of view | 2.78 arcmin |
| Plate scale at guider focal plane | 12.54 arcsec/mm |
| Operating temperature | −20 °C |
| Read noise | 3 e⁻ |
| Peak QE | ~80% |
| Dark current at −20 °C (estimated) | ~1 e⁻/pix/s |

## Proposed Detector: ASI294MM-P

| Parameter | Value (bin2 mode) |
|---|---|
| Sensor | Sony IMX492, monochrome |
| Pixel size | 4.63 µm |
| Resolution | 4144 × 2822 |
| Sensor dimensions | 19.1 × 13.0 mm |
| Read noise (HCG mode) | 1.2 e⁻ |
| Peak QE | ~90% |
| Dark current at −20 °C | 0.0022 e⁻/pix/s |
| Cooling | Two-stage TEC, ΔT ≈ 35 °C |
| Interface | USB3 |

## Field of View Analysis

The existing relay optics deliver an illuminated patch of ~2.78 arcmin diameter at the focal plane, equivalent to a ~13 mm circle. The ASI294MM-P sensor (19.1 × 13.0 mm) **fully contains this patch** with margin to spare.

**Conclusion:** Without optical modifications, the effective FOV remains ~2.78 arcmin. The unused sensor area outside the illuminated patch can be cropped in software. There is no FOV loss.

If, in a future phase, the relay optics are opened up, the sensor could in principle deliver up to ~4 × 2.7 arcmin — but this is not part of the proposed test.

## Pixel Sampling

With the existing plate scale (12.54 arcsec/mm), the ASI294MM-P delivers **0.058 arcsec/pix in bin2 mode** — roughly 3× finer than the current 0.163 arcsec/pix. For typical Las Campanas seeing (0.8–1.5″), this corresponds to ~15–25 pixels across the PSF FWHM, which is heavily oversampled but harmless. Software binning (e.g., 4×4) can be applied if desired to match the current sampling and reduce data volume.

## SNR Comparison

Both cameras are read-noise dominated in the guide-star aperture for typical exposure times. The lower per-pixel read noise of the ASI is largely offset by the larger pixel count needed to cover the PSF, so the dominant remaining factor is the **~10% higher quantum efficiency**.

For a 1″ seeing PSF, 1 s exposure, aperture of 2× FWHM:

| Guide star flux | CCD47 SNR | ASI294 SNR | Ratio |
|---|---|---|---|
| 1000 ph/s (bright) | 18.0 | 19.0 | 1.06× |
| 100 ph/s (nominal) | 2.25 | 2.38 | 1.06× |
| 30 ph/s (faint) | 0.69 | 0.73 | 1.06× |

The ~6% improvement is consistent across the brightness range and corresponds to what is expected from QE alone (√(0.90/0.80) ≈ 1.06).

### Sensitivity to assumptions

The conclusion is robust to uncertainty in the CCD47 dark current at −20 °C:

| Assumed D_CCD47 (e⁻/pix/s) | SNR ratio (ASI / CCD47) |
|---|---|
| 0.1 | 1.01× |
| 1.0 | 1.06× |
| 5.0 | 1.24× |

Across the plausible range, the ASI is **never worse** than the CCD47 and is **modestly to moderately better**.

## Other Considerations Beyond SNR

These are not part of the SNR case but argue in favor of the test:

- **Faster readout / higher cadence.** USB3 + small subframe enables tens of Hz, vs. the slower CCD47 readout chain.
- **Modern, supported interface.** ASCOM / INDI / native ZWO SDK — the existing GCAM stack and DSP electronics are aging.
- **Lower maintenance burden.** Commodity hardware with off-the-shelf replacement parts.
- **Negligible dark current** allows long exposures if ever needed for very faint guide stars (a regime where the CCD47 would also struggle but for different reasons).

## Risks and Open Questions

These are precisely the things the proposed test is meant to answer:

1. **Software integration.** The existing guider control loop and TCS hand-off were designed around GCAM. A new control path will need to be developed or adapted.
2. **Mechanical adapter.** The ASI294MM-P uses M42/M48 thread with ~17.5 mm back focus; an adapter to the existing port mount must be fabricated.
3. **Cooling stability.** The two-stage TEC must hold −20 °C reliably under summer ambient conditions at LCO. Needs a 12 V / 3 A external supply.
4. **Vignetting / illumination uniformity.** Confirm by on-sky flat that the optical footprint is as expected.
5. **Actual CCD47 dark current at −20 °C.** Worth measuring from existing dark frames to refine the SNR baseline. Does not affect the *direction* of the result, only its magnitude.

## Recommendation

A **side-by-side test** using the existing guider port — with no changes to the relay optics — is **low-risk, low-cost, and scientifically informative**. The expected outcome is a modest but real SNR improvement plus significant operational benefits (cadence, supportability). If the test is successful, a future phase could explore opening up the optics to take advantage of the larger sensor area.

The test fundamentally answers: *can we modernize this guider with a commodity camera and gain a small but consistent improvement?* The numbers say yes, and the test is the way to confirm it under real on-sky conditions.

## Notes on the Numbers

All SNR figures assume 1″ seeing, a 2×FWHM circular aperture, and a fiducial guide-star photon rate; the **ratios** between detectors are insensitive to these choices. The CCD47 dark current at −20 °C is estimated by scaling the +20 °C reference value with a 6–7 K silicon doubling law, cross-checked against the E2V CCD47-20 BI MPP datasheet, giving ~0.5–5 e⁻/pix/s. A mid-value of 1 e⁻/pix/s is used for the headline numbers; this can be confirmed with on-instrument darks before the test.
