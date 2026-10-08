// CubeView.h
// Desenha um CubeState na tela e anima os giros de camada.
// Não guarda o estado do cubo: ele mora no CubeState que você passa no construtor.
#pragma once
#include <Adafruit_GFX.h>
#include <Arduino.h>

#include "CubeState.h"
#include "TFT_FSMC.h"

/* Cores padrão de cada face (índice = CubeFace). Troque com setFaceColor(). */
#define CUBE_WHITE 0xFFFF
#define CUBE_RED 0xF800
#define CUBE_GREEN 0x07E0
#define CUBE_YELLOW 0xFFE0
#define CUBE_ORANGE 0xFD20
#define CUBE_BLUE 0x001F

class CubeView {
   public:
	CubeView(TFT_FSMC& tft, CubeState& cube, int16_t size = 200);

	bool begin();  // aloca o framebuffer (false se faltar RAM)

	/* --- Movimentos ---------------------------------------------------
	 * Os movimentos entram numa fila (até 32). A animação roda dentro de update()
	 * e o CubeState só é alterado quando cada giro TERMINA de animar. */
	bool move(uint8_t face, uint8_t turns = 1);  // false se a fila estiver cheia
	bool moves(const char* seq);                 // "R U R' U2 F" (false se inválido ou sem espaço; não enfileira nada)
	bool busy() const { return _animating || _qCount > 0; }
	void scramble(uint8_t movesCount);
	void cancelMoves();  // descarta o giro em andamento e a fila (sem aplicar)
	void setAnimation(bool enabled, uint16_t msPerQuarter = 250);

	/* --- Visual --------------------------------------------------------- */
	void setFaceColor(uint8_t face, uint16_t color);  // cor RGB565 da face (esquema de cores)
	void rotate(float dxDeg, float dyDeg);            // gira o cubo inteiro (dx = eixo X, dy = eixo Y)
	void setAngles(float axDeg, float ayDeg);
	void alignCameraToFace(uint8_t face);
	void invalidate() { _dirty = true; }  // chame se alterar o CubeState diretamente
	void setOrigin(int16_t x, int16_t y) {
		_x0 = x;
		_y0 = y;
		_customOrigin = true;
		_dirty = true;
	}

	uint8_t pending() const { return _qCount + (_animating ? 1 : 0); }  // giros que faltam (inclui o animando)

	void update();                    // chamar sempre no loop(): avança a animação e redesenha se preciso
	void render(bool force = false);  // só redesenha se algo mudou (update() já chama)

   private:
	void beginMove(uint32_t now);
	bool pushMove(CubeMove m);

	int16_t _x0 = 0, _y0 = 0;
	bool _customOrigin = false;

	TFT_FSMC& _tft;
	CubeState& _cube;
	GFXcanvas16* _canvas = nullptr;
	int16_t _size;
	float _scale;
	float _ax = 25.0f, _ay = -30.0f;
	uint16_t _palette[6];
	bool _dirty = true;

	// fila + animação
	static const uint8_t QSIZE = 64;
	CubeMove _queue[QSIZE];
	uint8_t _qHead = 0, _qCount = 0;
	bool _animEnabled = true;
	uint16_t _msQuarter = 250;
	bool _animating = false;
	CubeMove _cur = {0, 0};
	uint32_t _animStart = 0, _animDur = 0;
	float _targetDeg = 0, _layerDeg = 0;

	uint8_t _lastFace = 99;
	uint8_t _lastTop = FACE_U;
	uint8_t _lastFront = FACE_F;
	uint8_t _lastLat = FACE_R;
};