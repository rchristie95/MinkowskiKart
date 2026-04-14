//
//  Minkowski Kart - Blackboard Overlay
//  Screen-space overlay for the Cosmic String backward-fire gag.
//

#ifndef HEADER_BLACKBOARD_OVERLAY_HPP
#define HEADER_BLACKBOARD_OVERLAY_HPP

#include "utils/no_copy.hpp"
#include "utils/vec3.hpp"

class AbstractKart;

#ifndef SERVER_ONLY
namespace irr
{
    namespace video { class ITexture; }
    namespace scene { class IBillboardSceneNode; }
}
using namespace irr;
#endif

/**
 * Manages the blackboard overlay for the Cosmic String backward-fire gag.
 * Attached to the victim kart's camera. Slams in from an angle,
 * wobbles as they drive, and has wood frame + chalk equations.
 *
 * Equations shown:
 *   - Large central: E^2 = p^2*c^2 + m^2*c^4
 *   - Einstein field: R_uv - 1/2*g_uv*R = 8*pi*G*T_uv
 *   - Stress-energy: T^uv = (rho+p)u^u*u^v + p*g^uv
 *   - Geodesic: d^2x^u/dtau^2 + Gamma^u_{ab}*(dx^a/dtau)*(dx^b/dtau) = 0
 */
class BlackboardOverlay : public NoCopy
{
private:
    float m_opacity;          // 0=invisible, 1=full
    float m_wobble_time;      // accumulates for wobble animation
    float m_duration;         // ticks remaining
    int   m_owner_kart_id;    // which kart sees this

    // Slam-in animation state
    float m_slam_velocity;    // current vertical velocity for bounce
    float m_board_y_offset;   // current y-offset from final position
    float m_board_angle;      // slight tilt from the slam

    static const float SLAM_SPEED;      // pixels/sec initial slam speed
    static const float BOUNCE_DAMPING;  // how quickly wobble dies
    static const float BOARD_COVERAGE;  // fraction of screen covered

#ifndef SERVER_ONLY
    irr::video::ITexture *m_equations_texture;  // pre-baked equation texture
    void loadEquationsTexture();
#endif

public:
    explicit BlackboardOverlay(int owner_kart_id, float duration_seconds);
    ~BlackboardOverlay();

    void update(float dt);
    void render();

    bool isFinished() const { return m_duration <= 0 && m_opacity <= 0; }
    int getOwnerKartId() const { return m_owner_kart_id; }
};

#endif // HEADER_BLACKBOARD_OVERLAY_HPP
