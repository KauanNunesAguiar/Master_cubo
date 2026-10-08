// HudScreen.h — cubo + painel (a tela principal). Contém a lógica dos modos VISTA/FACE.
#pragma once
#include "CubeHUD.h"
#include "CubeView.h"
#include "Screen.h"

class HudScreen : public Screen {
   public:
	HudScreen(CubeView& cube, CubeHUD& hud) : _cube(cube), _hud(hud) {}

	void onEnter() override {
		_hud.invalidate();
		_cube.invalidate();
	}
	void update(uint32_t) override {
		_cube.update();
		_hud.updateUI();
	}
	bool canLeave() override { return !_hud.inputLocked(); }

	bool onInput(const InputEvent& e) override {
		switch (e.type) {
			case IN_ENC_ROT:
				if (_mode == VIEW)
					_cube.rotate(0, e.a > 0 ? 15.0f : -15.0f);
				else
					_hud.turnSelected(e.a > 0);
				return true;
			case IN_ENC_CLICK:
				if (_mode == VIEW)
					_hud.scramble();
				else
					_hud.selectFace((_hud.face() + 1) % 6);
				return true;
			case IN_ENC_LONG:
				setMode(_mode == VIEW ? FACE : VIEW);
				return true;
			case IN_K0:
				_hud.scramble();
				return true;
			case IN_K1:
				_hud.requestSolve();
				return true;
			case IN_TOUCH:
				_hud.onTouch(e.a, e.b);
				return true;
		}
		return false;
	}

   private:
	enum Mode : uint8_t { VIEW, FACE };
	CubeView& _cube;
	CubeHUD& _hud;
	Mode _mode = VIEW;

	void setMode(Mode m) {
		_mode = m;
		if (m == FACE) {
			_hud.selectFace(_hud.face());  // alinha a câmera na face atual
			_hud.notify("Modo: FACE");
		} else {
			_hud.notify("Modo: VISTA");
		}
	}
};