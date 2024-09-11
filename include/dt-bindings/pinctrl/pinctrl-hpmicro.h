
#ifndef __DT_BINDINGS_PINCTRL_HPMICRO_H__
#define __DT_BINDINGS_PINCTRL_HPMICRO_H__

/*
 * GPIOMUX bits:
 *  | 31          | 30        | 29 - 25       |     24    |     23 - 22  |  21  | 20 | 19 | 18  | 17  | 16 - 15 | 14 - 12 | 11 - 0 |
 *  |  loop back  |  analog   |  alt select   | hys       | prs          | ps   | pe | ke | od | sr | spd   | ds    | pin index  |
 *
 */
#define GPIOMUX(index, pe, ps, od) ( \
				(index & 0xFFF) | ((pe & 0x01) << 20) | ((ps & 0x01) << 21)| ((od & 0x01) << 18) \
)

#define ALTSELECT(index, alt) ( \
								(index & 0xFFF) | ((alt & 0x1F) << 25)		\
)
#define PIN_INDEX_PA(x) (x)
#define PIN_INDEX_PB(x) (x + 32)
#define PIN_INDEX_PC(x) (x + 32 * 2)
#define PIN_INDEX_PD(x) (x + 32 * 3)
#define PIN_INDEX_PE(x) (x + 32 * 4)
#define PIN_INDEX_PF(x) (x + 32 * 5)
#define PIN_INDEX_PX(x) (x + 32 * 6)
#define PIN_INDEX_PY(x) (x + 32 * 7)
#define PIN_INDEX_PZ(x) (x + 32 * 8)


#endif