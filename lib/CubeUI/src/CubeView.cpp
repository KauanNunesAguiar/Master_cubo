// CubeView.cpp
#include "CubeView.h"

#include <STM32FreeRTOS.h>
#include <math.h>
#include <stdlib.h>

#include <new>

#include "Metrics.h"

struct V3 {
	float x, y, z;
};

static const uint16_t FACE_COLOR[6] = {CUBE_WHITE, CUBE_RED, CUBE_GREEN, CUBE_YELLOW, CUBE_ORANGE, CUBE_BLUE};

/* Parâmetros visuais (unidades do cubo: o cubo todo vai de -CUBE_HALF a +CUBE_HALF) */
static const float CAM_D = 8.0f;          // distância da câmera
static const float CUBE_HALF = 1.5f;      // meia aresta do cubo
static const float TILE_HALF = 0.5f;      // meia aresta da "casa" escura de cada adesivo
static const float STICKER_HALF = 0.44f;  // meia aresta do adesivo colorido
static const float CAP_PLANE = 0.5f;      // plano interno que separa a camada que gira do resto
static const float VIEW_SCALE = 0.14f;    // pixels por unidade = size * VIEW_SCALE
static const uint16_t BODY_COLOR = 0x2104;

static inline V3 v3(const int8_t a[3]) { return {(float)a[0], (float)a[1], (float)a[2]}; }
static inline V3 madd(V3 a, V3 b, float k) { return {a.x + b.x * k, a.y + b.y * k, a.z + b.z * k}; }
static inline V3 neg(V3 a) { return {-a.x, -a.y, -a.z}; }
static inline float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

// Rotação da visão (câmera): gira em Y e depois em X
static V3 rotView(V3 v, float cx, float sx, float cy, float sy) {
	float x1 = v.x * cy + v.z * sy;
	float z1 = -v.x * sy + v.z * cy;
	float y2 = v.y * cx - z1 * sx;
	float z2 = v.y * sx + z1 * cx;
	return {x1, y2, z2};
}

// Rotação de um ângulo (cos c, sin s) em torno do eixo unitário a (Rodrigues)
static V3 rotAxis(V3 v, V3 a, float c, float s) {
	V3 cr = {a.y * v.z - a.z * v.y, a.z * v.x - a.x * v.z, a.x * v.y - a.y * v.x};
	float d = dot(a, v) * (1.0f - c);
	return {v.x * c + cr.x * s + a.x * d, v.y * c + cr.y * s + a.y * d, v.z * c + cr.z * s + a.z * d};
}

/* Lista de polígonos a desenhar (pintor: do mais longe para o mais perto).
 * Fica em memória estática para não pesar na pilha. */
struct Item {
	int16_t tx[4], ty[4];  // "casa" escura
	int16_t sx[4], sy[4];  // adesivo colorido
	uint16_t col;
	bool hasSticker;
	float z;
};
static Item g_items[56];  // 54 adesivos + 2 tampas
static uint8_t g_order[56];

/* ------------------------------------------------------------------ */

CubeView::CubeView(TFT_FSMC& tft, CubeState& cube, int16_t size) : _tft(tft), _cube(cube), _size(size) {
	_scale = size * VIEW_SCALE;
	for (uint8_t i = 0; i < 6; i++) _palette[i] = FACE_COLOR[i];
}

bool CubeView::begin() {
	_canvas = new (std::nothrow) GFXcanvas16(_size, _size);
	if (!_canvas || !_canvas->getBuffer()) {
		_canvas = nullptr;
		return false;
	}
	_dirty = true;
	return true;
}

void CubeView::setFaceColor(uint8_t face, uint16_t color) {
	if (face < 6) {
		_palette[face] = color;
		_dirty = true;
	}
}

void CubeView::rotate(float dxDeg, float dyDeg) { setAngles(_ax + dxDeg, _ay + dyDeg); }

void CubeView::setAngles(float axDeg, float ayDeg) {
	_ax = fmodf(axDeg, 360.0f);
	_ay = fmodf(ayDeg, 360.0f);
	_dirty = true;
}

/* ---------------------- fila e animação --------------------------- */

bool CubeView::pushMove(CubeMove m) {
	taskENTER_CRITICAL();

	if (_qCount >= QSIZE) {
		taskEXIT_CRITICAL();
		return false;
	}

	_queue[(_qHead + _qCount) % QSIZE] = m;
	_qCount++;
	taskEXIT_CRITICAL();
	return true;
}

bool CubeView::move(uint8_t face, uint8_t turns) {
	turns &= 3;
	if (face >= 6 || turns == 0) return false;
	return pushMove({face, turns});
}

bool CubeView::moves(const char* seq) {
	const char* p = seq;
	CubeMove m;
	uint8_t n = 0;
	while (CubeState::nextMove(p, m)) n++;
	if (*p) return false;                   // token inválido
	if (n > QSIZE - _qCount) return false;  // não cabe na fila
	p = seq;
	while (CubeState::nextMove(p, m)) pushMove(m);
	return true;
}

void CubeView::scramble(uint8_t movesCount) {
	int last = -1;
	for (uint8_t i = 0; i < movesCount; i++) {
		int f;
		do { f = rand() % 6; } while (f == last);
		last = f;
		if (!pushMove({(uint8_t)f, (uint8_t)(1 + rand() % 3)})) break;
	}
}

void CubeView::cancelMoves() {
	taskENTER_CRITICAL();
	_qCount = 0;
	taskEXIT_CRITICAL();

	_animating = false;
	_layerDeg = 0;
	_dirty = true;
}

void CubeView::setAnimation(bool enabled, uint16_t msPerQuarter) {
	_animEnabled = enabled;
	_msQuarter = msPerQuarter ? msPerQuarter : 1;
	if (!enabled && _animating) {  // termina o giro atual na hora
		_cube.applyMove(_cur);
		_animating = false;
		_layerDeg = 0;
		_dirty = true;
	}
}

void CubeView::beginMove(uint32_t now) {
	taskENTER_CRITICAL();
	_cur = _queue[_qHead];
	_qHead = (_qHead + 1) % QSIZE;
	_qCount--;
	taskEXIT_CRITICAL();

	_animating = true;
	_animStart = now;
	_animDur = (uint32_t)_msQuarter * (_cur.turns == 2 ? 2 : 1);
	_targetDeg = (_cur.turns == 1) ? -90.0f : (_cur.turns == 2) ? -180.0f : 90.0f;  // horário = negativo
	_layerDeg = 0;
}

void CubeView::update() {
	const uint32_t now = millis();

	if (!_animEnabled) {
		while (_qCount) {
			_cube.applyMove(_queue[_qHead]);
			taskENTER_CRITICAL();
			_qHead = (_qHead + 1) % QSIZE;
			_qCount--;
			taskEXIT_CRITICAL();

			_dirty = true;
		}
	} else {
		if (!_animating && _qCount) beginMove(now);
		if (_animating) {
			uint32_t el = now - _animStart;
			if (el >= _animDur) {
				_cube.applyMove(_cur);  // o estado muda quando o giro termina
				_animating = false;
				_layerDeg = 0;
				_dirty = true;
				if (_qCount) beginMove(now);
			} else {
				float t = (float)el / (float)_animDur;
				t = t * t * (3.0f - 2.0f * t);  // suaviza início e fim
				_layerDeg = _targetDeg * t;
				_dirty = true;
			}
		}
	}
	render();
}

/* --------------------------- desenho ------------------------------ */

void CubeView::render(bool force) {
	if (!_canvas || (!_dirty && !force)) return;
	_dirty = false;
	M_TIC(tRender);

	const float d2r = 0.0174532925f;
	const float cx = cosf(_ax * d2r), sx = sinf(_ax * d2r);
	const float cy = cosf(_ay * d2r), sy = sinf(_ay * d2r);
	const int16_t mid = _size / 2;

	// Giro da camada em andamento (se houver)
	const bool anim = _animating && _layerDeg != 0.0f;
	V3 axis = {0, 0, 0};
	float lc = 1.0f, ls = 0.0f;
	if (anim) {
		axis = v3(CUBE_FACE_GEOM[_cur.face].n);
		lc = cosf(_layerDeg * d2r);
		ls = sinf(_layerDeg * d2r);
	}

	auto project = [&](V3 p, int16_t& px, int16_t& py) {
		float k = CAM_D / (CAM_D - p.z);
		px = mid + (int16_t)lroundf(p.x * _scale * k);
		py = mid - (int16_t)lroundf(p.y * _scale * k);
	};
	auto visible = [&](V3 n, V3 p) { return n.x * (-p.x) + n.y * (-p.y) + n.z * (CAM_D - p.z) > 0.0f; };
	auto fillQuad = [&](const int16_t* qx, const int16_t* qy, uint16_t col) {
		_canvas->fillTriangle(qx[0], qy[0], qx[1], qy[1], qx[2], qy[2], col);
		_canvas->fillTriangle(qx[0], qy[0], qx[2], qy[2], qx[3], qy[3], col);
	};
	// monta os 4 cantos de um quadrado (centro ctr, direções cu/rv, meia aresta h)
	auto makeQuad = [&](V3 ctr, V3 cu, V3 rv, float h, int16_t* qx, int16_t* qy) {
		static const int8_t sgn[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
		for (uint8_t i = 0; i < 4; i++) project(madd(madd(ctr, cu, sgn[i][0] * h), rv, sgn[i][1] * h), qx[i], qy[i]);
	};

	uint8_t count = 0;

	/* Tampas escuras: aparecem no meio do giro, onde a camada abre um "buraco" no cubo */
	if (anim) {
		const CubeFaceGeom& g = CUBE_FACE_GEOM[_cur.face];
		V3 cu = v3(g.u), rv = v3(g.v);
		V3 ctr = {axis.x * CAP_PLANE, axis.y * CAP_PLANE, axis.z * CAP_PLANE};
		for (uint8_t side = 0; side < 2; side++) {
			V3 n = side ? neg(axis) : axis;  // side 0: parte parada (normal +eixo); side 1: face da camada que gira
			V3 u2 = cu, v2 = rv;
			if (side) {
				u2 = rotAxis(u2, axis, lc, ls);
				v2 = rotAxis(v2, axis, lc, ls);
				n = rotAxis(n, axis, lc, ls);
			}
			n = rotView(n, cx, sx, cy, sy);
			u2 = rotView(u2, cx, sx, cy, sy);
			v2 = rotView(v2, cx, sx, cy, sy);
			V3 p = rotView(ctr, cx, sx, cy, sy);
			if (!visible(n, p)) continue;
			Item& it = g_items[count];
			makeQuad(p, u2, v2, CUBE_HALF, it.tx, it.ty);
			it.col = BODY_COLOR;
			it.hasSticker = false;
			it.z = p.z - 0.5f;  // sempre antes dos adesivos
			g_order[count] = count;
			count++;
		}
	}

	/* Adesivos */
	for (uint8_t f = 0; f < 6; f++) {
		const CubeFaceGeom& g = CUBE_FACE_GEOM[f];
		for (uint8_t i = 0; i < 9; i++) {
			const float c = (float)(i % 3) - 1.0f, r = (float)(i / 3) - 1.0f;
			V3 n = v3(g.n), cu = v3(g.u), rv = v3(g.v);
			V3 ctr = madd(madd({n.x * CUBE_HALF, n.y * CUBE_HALF, n.z * CUBE_HALF}, cu, c), rv, r);

			if (anim && dot(ctr, axis) > 0.5f) {  // adesivo da camada que está girando
				ctr = rotAxis(ctr, axis, lc, ls);
				n = rotAxis(n, axis, lc, ls);
				cu = rotAxis(cu, axis, lc, ls);
				rv = rotAxis(rv, axis, lc, ls);
			}
			n = rotView(n, cx, sx, cy, sy);
			cu = rotView(cu, cx, sx, cy, sy);
			rv = rotView(rv, cx, sx, cy, sy);
			ctr = rotView(ctr, cx, sx, cy, sy);

			if (!visible(n, ctr)) continue;  // de costas para a câmera

			Item& it = g_items[count];
			makeQuad(ctr, cu, rv, TILE_HALF, it.tx, it.ty);
			makeQuad(ctr, cu, rv, STICKER_HALF, it.sx, it.sy);
			it.col = _palette[_cube.get(f, i) % 6];
			it.hasSticker = true;
			it.z = ctr.z;
			g_order[count] = count;
			count++;
		}
	}

	/* Ordena do mais longe (z menor) para o mais perto (insertion sort: poucos itens) */
	for (uint8_t a = 1; a < count; a++) {
		uint8_t key = g_order[a];
		int8_t b = (int8_t)a - 1;
		while (b >= 0 && g_items[g_order[b]].z > g_items[key].z) {
			g_order[b + 1] = g_order[b];
			b--;
		}
		g_order[b + 1] = key;
	}

	_canvas->fillScreen(0);
	for (uint8_t k = 0; k < count; k++) {
		const Item& it = g_items[g_order[k]];
		fillQuad(it.tx, it.ty, BODY_COLOR);
		if (it.hasSticker) fillQuad(it.sx, it.sy, it.col);
	}

	int16_t x0 = _customOrigin ? _x0 : (_tft.width() - _size) / 2;
	int16_t y0 = _customOrigin ? _y0 : (_tft.height() - _size) / 2;
	M_TIC(tPush);
	_tft.setAddrWindow(x0, y0, _size, _size);
	_tft.pushColors(_canvas->getBuffer(), (uint32_t)_size * _size);
	metricsRender(M_TOC(tRender), M_TOC(tPush));
}

void CubeView::alignCameraToFace(uint8_t face) {
	if (face >= 6) return;
	if (_lastFace == face) return;
	_lastFace = face;

	if ((face == FACE_U) || (face == FACE_D)) _lastTop = face;
	if ((face == FACE_F) || (face == FACE_B)) _lastFront = face;
	if ((face == FACE_R) || (face == FACE_L)) _lastLat = face;

	float ax = 0.0f, ay = 0.0f;

	if (_lastFront == FACE_F) ay = 45.0f;
	if (_lastFront == FACE_B) ay = 135.0f;
	if (_lastLat == FACE_R) ay *= -1.0f;

	if (_lastTop == FACE_U) ax = 45.0f;
	if (_lastTop == FACE_D) {
		ax = -135.0f;
		ay += 180.0f;
	}

	setAngles(ax, ay);
}