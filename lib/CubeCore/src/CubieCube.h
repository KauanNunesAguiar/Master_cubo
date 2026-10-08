// CubieCube.h — cubo em nível de peças (8 cantos, 12 arestas)
#pragma once
#include <stdint.h>

#include "CubeState.h"

/* Cantos (posição/peça): 0 URF, 1 UFL, 2 ULB, 3 UBR, 4 DFR, 5 DLF, 6 DBL, 7 DRB
 * Arestas:  0 UR, 1 UF, 2 UL, 3 UB, 4 DR, 5 DF, 6 DL, 7 DB, 8 FR, 9 FL, 10 BL, 11 BR */
struct CubieCube {
	uint8_t cp[8], co[8];    // canto na posição i e sua torção (0..2)
	uint8_t ep[12], eo[12];  // aresta na posição i e sua inversão (0..1)

	CubieCube() { reset(); }
	void reset();

	void multiply(const CubieCube& b);                        // this = this * b (aplica b depois)
	void multiplyP1(const CubieCube& a, const CubieCube& b);  // this = a*b só em co, eo, ep (cp NÃO é atualizado)
	void multiplyP2(const CubieCube& a, const CubieCube& b);  // this = a*b só em cp, ep (fase 2: co/eo ignorados)
	bool fromFacelets(const CubeState& s);                    // false se alguma peça não puder ser identificada
	void toFacelets(CubeState& s) const;
	int verify() const;  // 0 = ok; negativos = erro (códigos do Kociemba)

	static void initMoves();                     // chamar uma vez (gera os 6 giros básicos)
	static const CubieCube& move(uint8_t face);  // giro horário de 90° da face
};