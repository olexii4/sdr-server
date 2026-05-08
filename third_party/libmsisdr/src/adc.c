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

int msisdr_adc_init (msisdr_dev_t *p) {
    if (!p) goto failed;

    /* inicializace - statická */
    msisdr_write_reg(p, 0x08, 0x006080); /* kernel driver */
    msisdr_write_reg(p, 0x05, 0x00000c);
    msisdr_write_reg(p, 0x00, 0x000200);
    msisdr_write_reg(p, 0x02, 0x004801);
    msisdr_write_reg(p, 0x08, 0x00f380); /* kernel driver */

    return 0;

failed:
    return -1;
}

int msisdr_adc_stop (msisdr_dev_t *p) {
    if (!p) goto failed;

    /* uspíme USB IF a ADC */
    msisdr_write_reg(p, 0x03, 0x010000);

    return 0;

failed:
    return -1;
}
