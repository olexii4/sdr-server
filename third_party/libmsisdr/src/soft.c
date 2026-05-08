/*
 * Copyright (C) 2013 by Miroslav Slugen <thunder.m@email.cz
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "soft.h"

//float band_limits[] = {
//        0.,     12.,    30.,    50.,    108.,   250.,   390.,   960.,   2400,   -1.
//};
//
//uint32_t band_select[] = {
//        0xf780, 0xff80, 0xf280, 0xf380, 0xfa80, 0xf680, 0xf380, 0xfa80, 0x0000, 0x0000
//};
//GPIO0 - DAB notch
//GPIO2 - Broadcast FM notch

hw_switch_freq_plan_t hw_switch_freq_plan_default[] = {
        {0,    MSISDR_MODE_AM,  MSISDR_UPCONVERT_MIXER_ON, MSISDR_AM_PORT2, 16, 0xf780},
        {12,   MSISDR_MODE_AM,  MSISDR_UPCONVERT_MIXER_ON, MSISDR_AM_PORT2, 16, 0xff80},
        {30,   MSISDR_MODE_AM,  MSISDR_UPCONVERT_MIXER_ON, MSISDR_AM_PORT2, 16, 0xf280},
        {50,   MSISDR_MODE_VHF, 0, 0, 32, 0xf380},
        {108,  MSISDR_MODE_B3,  0, 0, 16, 0xfa80},
        {250,  MSISDR_MODE_B3,  0, 0, 16, 0xf680},
        {259,  6              ,  0, 0, 8,  0xf680},
        {330,  MSISDR_MODE_B45, 0, 0, 4,  0xf380},
        {960,  MSISDR_MODE_BL,  0, 0, 2,  0xfa80},
        {2400, -1, 0, 0, 0, 0x0000},
};

hw_switch_freq_plan_t hw_switch_freq_plan_sdrplay[] = {
        {0,    MSISDR_MODE_AM,  MSISDR_UPCONVERT_MIXER_ON, MSISDR_AM_PORT2, 16, 0xf580},
        {12,   MSISDR_MODE_AM,  MSISDR_UPCONVERT_MIXER_ON, MSISDR_AM_PORT2, 16, 0xf580},
        {30,   MSISDR_MODE_AM,  MSISDR_UPCONVERT_MIXER_ON, MSISDR_AM_PORT2, 16, 0xf580},
        {50,   MSISDR_MODE_VHF, 0, 0, 32, 0xf180},
        {112,  MSISDR_MODE_B3,  0, 0, 16, 0xf580},
        {250,  MSISDR_MODE_B3,  0, 0, 16, 0xf480},
        {261,  6              ,  0, 0, 8,  0xf480},
        {404,  MSISDR_MODE_B45, 0, 0, 4,  0xf580},
        {1000, MSISDR_MODE_BL,  0, 0, 2,  0xf580},
        {2400, -1, 0, 0, 0, 0x0000},
};

hw_switch_freq_plan_t *hw_switch_freq_plan[2] = {
        hw_switch_freq_plan_default,
        hw_switch_freq_plan_sdrplay
};

#define BIAS_GPIO 3

void update_reg_8(msisdr_dev_t *p)
{
    msisdr_write_reg(p, 0x08, p->reg8|(p->bias?(1<<(BIAS_GPIO+8)):0));
}

int msisdr_set_soft(msisdr_dev_t *p)
{
    uint32_t reg0 = 0, reg2 = 2, reg5 = 5, reg3 = 3, regd = 0x0d;
    uint64_t n, thresh, frac, lo_div = 0, fvco = 0, rfvco = 0, offset = 0, afc = 0, a, b, c;
    int i;

    /*** registr0 - parametry pásma ***/
    /*** registr0 - parameters zone ***/

    /* pásmo */
    /* zone */

    i = 0;

    while (p->freq >= 1000000 * hw_switch_freq_plan[(int) p->hw_flavour][i].low_cut)
    {
        if (hw_switch_freq_plan[(int) p->hw_flavour][i].mode < 0) {
            break;
        }

        i++;
    }

    hw_switch_freq_plan_t switch_plan = hw_switch_freq_plan[(int) p->hw_flavour][i-1];

#if MSISDR_DEBUG >= 1
    fprintf(stderr, "msisdr_set_soft: i:%d flavour:%d flow:%u mode:%d up:%d port:%d lo:%d\n",
            i-1,
            (int) p->hw_flavour,
            switch_plan.low_cut,
            switch_plan.mode,
            switch_plan.upconvert_mixer_on,
            switch_plan.am_port,
            switch_plan.lo_div);
#endif

    if (switch_plan.mode == MSISDR_MODE_AM)
    {
        reg0 |= MSISDR_MODE_AM << 4;
        reg0 |= switch_plan.upconvert_mixer_on << 9;
        reg0 |= switch_plan.am_port << 11;

        if (switch_plan.upconvert_mixer_on)
        {
            offset += 120000000UL;
        }

        lo_div = 16;

        if (switch_plan.am_port == 0) {
            p->band = MSISDR_BAND_AM1;
        } else {
            p->band = MSISDR_BAND_AM2;
        }
    }
    else
    {
        reg0 |= switch_plan.mode << 4;
        lo_div = switch_plan.lo_div;

        if (switch_plan.mode == MSISDR_MODE_VHF) {
            p->band = MSISDR_BAND_VHF;
        } else if (switch_plan.mode == MSISDR_MODE_B3) {
            p->band = MSISDR_BAND_3;
        } else if (switch_plan.mode == MSISDR_MODE_B45) {
            p->band = MSISDR_BAND_45;
        } else if (switch_plan.mode == MSISDR_MODE_BL) {
            p->band = MSISDR_BAND_L;
        }
    }

//    if (p->freq < 50000000)
//    {
//        /* AM režim2, antena 2, AM režim1 - 0x61 */
//        /* AM mode2, Antena 2, AM mode1 - 0x61   */
//        reg0 |= MSISDR_MODE_AM << 4;
//        reg0 |= MSISDR_UPCONVERT_MIXER_ON << 9;
//        reg0 |= MSISDR_AM_PORT2 << 11;
//        /* AM režim je posunutý o 5 * referenční frekvence, tj. o 120 MHz */
//        /* AM mode is shifted about 5 * reference frequency, i.e. 120 MHz */
//        lo_div = 16;
//        offset += 120000000UL;
//    }
//    else if (p->freq < 108000000)
//    {
//        /* VHF */
//        reg0 |= MSISDR_MODE_VHF << 4;
//        lo_div = 32;
//    }
//    else if (p->freq < 330000000)
//    {
//        /* B3 */
//        reg0 |= MSISDR_MODE_B3 << 4;
//        lo_div = 16;
//    }
//    else if (p->freq < 960000000)
//    {
//        /* B45 */
//        reg0 |= MSISDR_MODE_B45 << 4;
//        lo_div = 4;
//    }
//    else
//    {
//        /* BL */
//        reg0 |= MSISDR_MODE_BL << 4;
//        lo_div = 2;
//    }

    /* RF syntetizer je vždy aktivní */
    /* RF synthesizer is always active */
    reg0 |= MSISDR_RF_SYNTHESIZER_ON << 10;

    /* režim IF filtru - zatím nefunguje? */
    /* IF filter mode - has not worked? */
    switch (p->if_freq)
    {
    case MSISDR_IF_ZERO:
        reg0 |= MSISDR_IF_MODE_ZERO << 12;
        break;
    case MSISDR_IF_450KHZ:
        reg0 |= MSISDR_IF_MODE_450KHZ << 12;
        break;
    case MSISDR_IF_1620KHZ:
        reg0 |= MSISDR_IF_MODE_1620KHZ << 12;
        break;
    case MSISDR_IF_2048KHZ:
        reg0 |= MSISDR_IF_MODE_2048KHZ << 12;
        break;
    }

    /* šířka pásma - 8 MHz, nejvyšší možná */
    /* Bandwidth - 8 MHz, the highest possible */
    switch (p->bandwidth)
    {
    case MSISDR_BW_200KHZ:
        reg0 |= 0x00 << 14;
        break;
    case MSISDR_BW_300KHZ:
        reg0 |= 0x01 << 14;
        break;
    case MSISDR_BW_600KHZ:
        reg0 |= 0x02 << 14;
        break;
    case MSISDR_BW_1536KHZ:
        reg0 |= 0x03 << 14;
        break;
    case MSISDR_BW_5MHZ:
        reg0 |= 0x04 << 14;
        break;
    case MSISDR_BW_6MHZ:
        reg0 |= 0x05 << 14;
        break;
    case MSISDR_BW_7MHZ:
        reg0 |= 0x06 << 14;
        break;
    case MSISDR_BW_8MHZ:
        reg0 |= 0x07 << 14;
        break;
    case MSISDR_BW_MAX:
        reg0 |= 0x07 << 14;
        regd |= (1<<4);
        break;
    }

    /* xtal frekvence - nepodporujeme změnu */
    /* xtal frequency - we do not support change */
    switch (p->xtal)
    {
    case MSISDR_XTAL_19_2M:
        reg0 |= 0x00 << 17;
        break;
    case MSISDR_XTAL_22M:
        reg0 |= 0x01 << 17;
        break;
    case MSISDR_XTAL_24M:
    case MSISDR_XTAL_24_576M:
        reg0 |= 0x02 << 17;
        break;
    case MSISDR_XTAL_26M:
        reg0 |= 0x03 << 17;
        break;
    case MSISDR_XTAL_38_4M:
        reg0 |= 0x04 << 17;
        break;
    }

    /* 4 bity pro režimy snížené spotřeby */
    /* 4 bits for power saving modes */
    reg0 |= MSISDR_IF_LPMODE_NORMAL << 20;
    reg0 |= MSISDR_VCO_LPMODE_NORMAL << 23;

    /* vco frekvence, je lepší použít 64bitový rozsah */
    /* VCO frequency is better to use a 64-bit range */
    fvco = (p->freq + offset) * lo_div;

    /* posun po hlavní frekvenci */
    /* shift the main frequency */
    n = fvco / 96000000UL;

    /* hlavní registr, hrubé ladění */
    /* major registry, coarse tuning */
    thresh = 96000000UL / lo_div;

    /* vedlejší registr, jemné ladění */
    /* side register, fine tuning */
    frac = (fvco % 96000000UL) / lo_div;

    /* najdeme největší společný dělitel pro thresh a frac */
    /* We find the greatest common divisor for thresh and frac */
    for (a = thresh, b = frac; a != 0;)
    {
        c = a;
        a = b % a;
        b = c;
    }

    /* dělíme */
    /* divided */
    thresh /= b;
    frac /= b;

    /* v této části musíme rozlišení snížit na maximální rozsah registru */
    /* In this section we reduce the resolution to the maximum extent registry */
    a = (thresh + 4094) / 4095;
    thresh = (thresh + (a / 2)) / a;
    frac = (frac + (a / 2)) / a;

    rfvco=(96000000UL * (n * thresh * 4096UL + (frac * 4096UL))) / (thresh * 4096UL * lo_div);
    if(p->freq + offset < rfvco)
        frac --;
    rfvco=(96000000UL * (n * thresh * 4096UL + (frac * 4096UL + afc))) / (thresh * 4096UL * lo_div);
    afc = ((p->freq + offset - rfvco) * thresh * 4096UL * lo_div) /96000000UL;

    reg3 |= (afc & 4095) << 4;
    reg5 |= (0xFFF & thresh) << 4;
    /* rezervováno, musí být 0x28 */
    /* Reserved, must be 0x28 */
    reg5 |= MSISDR_RF_SYNTHESIZER_RESERVED_PROGRAMMING << 16;

    reg2 |= (0xFFF & frac) << 4;
    reg2 |= (0x3F & n) << 16;
    reg2 |= MSISDR_LBAND_LNA_CALIBRATION_OFF << 22;

    /* kernel driver nastavuje až při změně frekvence */
    /* kernel driver adjusts to changing frequencies  */

//    i = 0;
//
//    while (p->freq >= 1000000 * band_limits[i + 1]) {
//        i++;
//    }
//
//    if (band_select[i] != 0)
//    {
//        msisdr_write_reg(p, 0x08, 0xf380);
//        msisdr_write_reg(p, 0x08, 0x6280);
//        msisdr_write_reg(p, 0x08, band_select[i]);
//    }

    //msisdr_write_reg(p, 0x08, switch_plan.band_select_word);
    p->reg8=switch_plan.band_select_word;
    update_reg_8(p);

    msisdr_write_reg(p, 0x09, 0x0e);
    msisdr_write_reg(p, 0x09, reg3);

    msisdr_write_reg(p, 0x09, reg0);
    msisdr_write_reg(p, 0x09, reg5);
    msisdr_write_reg(p, 0x09, reg2);
    msisdr_write_reg(p, 0x09, regd);

//    if (band_select[i] != 0)
//    {
//        msisdr_write_reg(p, 0x08, 0xf380);
//        msisdr_write_reg(p, 0x08, 0x6280);
//        msisdr_write_reg(p, 0x08, band_select[i]);
//        msisdr_write_reg(p, 0x09, 0x0e);
//        msisdr_write_reg(p, 0x09, 0x03);
//
//        msisdr_write_reg(p, 0x09, reg0);
//        msisdr_write_reg(p, 0x09, reg5);
//        msisdr_write_reg(p, 0x09, reg2);
//    }

#if MSISDR_DEBUG >= 1
    fprintf( stderr,"sel:%d %x ",i,switch_plan.band_select_word);
    fprintf( stderr,"freq: %.2f MHz (offset: %.2f MHz), n: %lu, fraction: %lu/%lu\n",
            ((double) n + (double) frac / (double) thresh) * 96.0 / (double) lo_div,
            (double) offset / 1.0e6, (long unsigned)n,
            (long unsigned)frac, (long unsigned)thresh);
#endif

    return 0;
}

int msisdr_set_center_freq(msisdr_dev_t *p, uint32_t freq)
{
    p->freq = freq;
    int r = msisdr_set_soft(p);
    r += msisdr_set_gain(p); // restore gain
    return r;
}

uint32_t msisdr_get_center_freq(msisdr_dev_t *p)
{
    return p->freq;
}

int msisdr_set_if_freq(msisdr_dev_t *p, uint32_t freq)
{
    if (!p)
        goto failed;

    switch (freq)
    {
    case 0:
        p->if_freq = MSISDR_IF_ZERO;
        break;
    case 450000:
        p->if_freq = MSISDR_IF_450KHZ;
        break;
    case 1620000:
        p->if_freq = MSISDR_IF_1620KHZ;
        break;
    case 2048000:
        p->if_freq = MSISDR_IF_2048KHZ;
        break;
    default:
        fprintf(stderr, "unsupported if frequency: %u Hz\n", freq);
        goto failed;
    }

    int r = msisdr_set_soft(p);
    r += msisdr_set_gain(p); // restore gain
    return r;

    failed: return -1;
}

uint32_t msisdr_get_if_freq(msisdr_dev_t *p)
{
    if (!p)
        goto failed;

    switch (p->if_freq)
    {
    case MSISDR_IF_ZERO:
        return 0;
    case MSISDR_IF_450KHZ:
        return 450000;
    case MSISDR_IF_1620KHZ:
        return 1620000;
    case MSISDR_IF_2048KHZ:
        return 2048000;
    }

    failed: return -1;
}

/* not supported yet */
int msisdr_set_xtal_freq(msisdr_dev_t *p, uint32_t freq)
{
    (void) p;
    (void) freq;
    return -1;
}

uint32_t msisdr_get_xtal_freq(msisdr_dev_t *p)
{
    if (!p)
        goto failed;

    switch (p->xtal)
    {
    case MSISDR_XTAL_19_2M:
        return 19200000;
    case MSISDR_XTAL_22M:
        return 22000000;
    case MSISDR_XTAL_24M:
    case MSISDR_XTAL_24_576M:
        /* realně 24 MHz ??? */
        return 24000000;
    case MSISDR_XTAL_26M:
        return 26000000;
    case MSISDR_XTAL_38_4M:
        return 38400000;
    }

    failed: return -1;
}

int msisdr_set_bandwidth(msisdr_dev_t *p, uint32_t bw)
{
    if (!p)
        return -1;

    p->bandwidth = MSISDR_BW_MAX;

    if(bw <= 8000000)
        p->bandwidth = MSISDR_BW_8MHZ;
    if(bw <= 7000000)
        p->bandwidth = MSISDR_BW_7MHZ;
    if(bw <= 6000000)
        p->bandwidth = MSISDR_BW_6MHZ;
    if(bw <= 5000000)
        p->bandwidth = MSISDR_BW_5MHZ;
    if(bw <= 1536000)
    {
        p->bandwidth = MSISDR_BW_1536KHZ;
        if(p->rate >= 5000000)
            p->if_freq = MSISDR_IF_1620KHZ;
    }
    if(bw <= 600000)
    {
        p->bandwidth = MSISDR_BW_600KHZ;
        p->if_freq = MSISDR_IF_450KHZ;
    }
    if(bw <= 300000)
    {
        p->bandwidth = MSISDR_BW_300KHZ;
        p->if_freq = MSISDR_IF_450KHZ;
    }
    if(bw <= 200000)
    {
        p->bandwidth = MSISDR_BW_200KHZ;
        p->if_freq = MSISDR_IF_450KHZ;
    }
    int r = msisdr_set_soft(p);
    r += msisdr_set_gain(p); // restore gain
    return r;
}

uint32_t msisdr_get_bandwidth(msisdr_dev_t *p)
{
    if (!p)
        goto failed;

    switch (p->bandwidth)
    {
    case MSISDR_BW_200KHZ:
        return 200000;
    case MSISDR_BW_300KHZ:
        return 300000;
    case MSISDR_BW_600KHZ:
        return 600000;
    case MSISDR_BW_1536KHZ:
        return 1536000;
    case MSISDR_BW_5MHZ:
        return 5000000;
    case MSISDR_BW_6MHZ:
        return 6000000;
    case MSISDR_BW_7MHZ:
        return 7000000;
    case MSISDR_BW_8MHZ:
        return 8000000;
    case MSISDR_BW_MAX:
        return 14000000;
    }

    failed: return -1;
}

int msisdr_set_freq_correction(msisdr_dev_t *p, int ppm)
{
    (void) p;
    (void) ppm;
    fprintf(stderr, "frequency correction not implemented yet\n");
    return -1;
}

int msisdr_set_direct_sampling(msisdr_dev_t *p, int on)
{
    (void) p;
    (void) on;
    fprintf(stderr, "direct sampling not implemented yet\n");
    return -1;
}

int msisdr_set_offset_tuning(msisdr_dev_t *p, int on)
{
    if (!p)
        goto failed;

    if (on)
    {
        p->if_freq = MSISDR_IF_450KHZ;
    }
    else
    {
        p->if_freq = MSISDR_IF_ZERO;
    }

    return msisdr_set_soft(p);

    failed: return -1;
}

int msisdr_set_transfer(msisdr_dev_t *p, const char *v)
{
    if (!p)
        goto failed;

    if (!strcmp(v, "BULK"))
    {
        p->transfer = MSISDR_TRANSFER_BULK;
    }
    else if (!strcmp(v, "ISOC"))
    {
        p->transfer = MSISDR_TRANSFER_ISOC;
    }
    else
    {
        fprintf(stderr, "unsupported transfer type: %s\n", v);
        goto failed;
    }

    return 0;

    failed: return -1;
}

const char *msisdr_get_transfer(msisdr_dev_t *p)
{
    switch (p->transfer)
    {
    case MSISDR_TRANSFER_BULK:
        return "BULK";
    case MSISDR_TRANSFER_ISOC:
        return "ISOC";
    }

    return "";
}

msisdr_band_t msisdr_get_band (msisdr_dev_t *p)
{
    return p->band;
}

int msisdr_set_bias (msisdr_dev_t *p, int bias)
{
	p->bias=bias;
	update_reg_8(p);
	return 0;
}

int msisdr_get_bias (msisdr_dev_t *p)
{
	return p->bias;
}
