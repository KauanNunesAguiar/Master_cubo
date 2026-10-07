// CubeState.h
// Modelo lógico do cubo mágico (sem nenhuma dependência de tela).
//
// Estado = 54 adesivos na ordem Kociemba: U(0-8) R(9-17) F(18-26) D(27-35) L(36-44) B(45-53).
// Cada adesivo guarda o ID da face de origem da sua cor (FACE_U..FACE_B), não uma cor RGB.
// Dentro de cada face: índice = linha*3 + coluna, visto de fora da face.
#pragma once
#include <stdint.h>

enum CubeFace : uint8_t { FACE_U, FACE_R, FACE_F, FACE_D, FACE_L, FACE_B };

/* Geometria de cada face em coordenadas do cubo (cubo vai de -1.5 a +1.5):
 *  n = normal (para fora), u = direção das colunas, v = direção das linhas.
 * É a única fonte de verdade: usada para gerar as permutações e para o desenho. */
struct CubeFaceGeom {
	int8_t n[3];
	int8_t u[3];
	int8_t v[3];
};
extern const CubeFaceGeom CUBE_FACE_GEOM[6];

/* Movimento de face: turns = 1 (90° horário), 2 (180°), 3 (90° anti-horário).
 * Notação: "R" = {FACE_R,1}, "R2" = {FACE_R,2}, "R'" = {FACE_R,3} */
struct CubeMove {
	uint8_t face;
	uint8_t turns;
};

class CubeState {
   public:
	CubeState() { reset(); }

	void reset();  // cubo resolvido

	uint8_t get(uint8_t face, uint8_t idx) const { return _s[face * 9 + idx]; }
	void set(uint8_t face, uint8_t idx, uint8_t faceId) { _s[face * 9 + idx] = faceId; }

	/* Movimentos */
	void applyMove(uint8_t face, uint8_t turns = 1);
	void applyMove(CubeMove m) { applyMove(m.face, m.turns); }
	bool applyMoves(const char* seq);  // "R U R' U2 F". Se houver erro, não aplica nada e retorna false.
	void scramble(uint8_t movesCount = 20);

	/* Lê um movimento de *p e avança p. Retorna false no fim da string ou se for inválido
	 * (nesse caso, *p aponta para o caractere ruim; no fim da string, *p == 0). */
	static bool nextMove(const char*& p, CubeMove& m);
	static CubeMove inverse(CubeMove m) { return {m.face, (uint8_t)((4 - m.turns) & 3)}; }

	/* Consulta / conversão */
	bool isSolved() const;
	void toString(char out[55]) const;  // 54 letras "URFDLB" + '\0' (formato do Kociemba)
	bool fromString(const char* s);     // aceita 54 letras U R F D L B (não valida se é resolvível)

   private:
	uint8_t _s[54];
};