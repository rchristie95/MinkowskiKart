//
//  Minkowski Kart - Blackboard Overlay
//  Implementation of the Cosmic String backward-fire blackboard screen effect.
//

#include "graphics/blackboard_overlay.hpp"

#ifndef SERVER_ONLY
#include "graphics/irr_driver.hpp"
#include "config/user_config.hpp"
#include "guiengine/engine.hpp"
#endif

#include "karts/abstract_kart.hpp"
#include "karts/controller/controller.hpp"
#include "modes/world.hpp"
#include "utils/constants.hpp"

#ifndef SERVER_ONLY
#include <IVideoDriver.h>
#include <ITexture.h>
#endif

#include <cmath>
#include <algorithm>

const float BlackboardOverlay::SLAM_SPEED    = 2.5f;
const float BlackboardOverlay::BOUNCE_DAMPING = 4.0f;
const float BlackboardOverlay::BOARD_COVERAGE = 0.75f; // 75% screen coverage

BlackboardOverlay::BlackboardOverlay(int owner_kart_id, float duration_seconds)
    : m_opacity(0)
    , m_wobble_time(0)
    , m_duration(duration_seconds)
    , m_owner_kart_id(owner_kart_id)
    , m_slam_velocity(SLAM_SPEED)
    , m_board_y_offset(-1.2f)   // starts off-screen above
    , m_board_angle(0.08f)      // slight initial tilt
{
#ifndef SERVER_ONLY
    m_equations_texture = nullptr;
    loadEquationsTexture();
#endif
}

BlackboardOverlay::~BlackboardOverlay()
{
#ifndef SERVER_ONLY
    // Texture managed by material_manager
#endif
}

#ifndef SERVER_ONLY
void BlackboardOverlay::loadEquationsTexture()
{
    // Try to load pre-rendered equation texture
    const std::string path = file_manager->getAsset("textures/blackboard_equations.png");
    if (!path.empty())
    {
        m_equations_texture = irr_driver->getTexture(path);
    }
    // If no texture, render will use a generated placeholder
}
#endif

void BlackboardOverlay::update(float dt)
{
    if (m_duration <= 0 && m_opacity <= 0) return;

    // --- Phase 1: Slam in ---
    if (m_board_y_offset < 0.0f)
    {
        // Accelerate downward like it was thrown from a lecture hall
        m_slam_velocity -= 8.0f * dt;  // gravity acceleration
        m_board_y_offset += m_slam_velocity * dt;

        if (m_board_y_offset >= 0.0f)
        {
            // Impact! Start bounce
            m_board_y_offset = 0.0f;
            m_slam_velocity = std::abs(m_slam_velocity) * 0.4f; // bounce back
            m_wobble_time = 0.0f;
        }
        m_opacity = std::min(1.0f, m_opacity + dt * 3.0f);
    }
    // --- Phase 2: Bounce and wobble ---
    else if (m_slam_velocity > 0.01f)
    {
        m_slam_velocity -= BOUNCE_DAMPING * dt;
        if (m_slam_velocity < 0) m_slam_velocity = 0;
        m_board_y_offset = m_slam_velocity * 0.1f;  // small bounce
        m_wobble_time += dt;
        // Angle wobble
        m_board_angle = 0.03f * std::sin(m_wobble_time * 8.0f)
                      * std::exp(-m_wobble_time * 3.0f);
    }
    // --- Phase 3: Active (board covers view, kart drives with it) ---
    else
    {
        m_duration -= dt;
        m_wobble_time += dt;

        // Gentle sway as kart moves
        m_board_angle = 0.015f * std::sin(m_wobble_time * 2.5f);
        m_board_y_offset = 0.005f * std::sin(m_wobble_time * 1.8f);

        // Fade out as duration expires
        if (m_duration < 1.0f)
        {
            m_opacity = std::max(0.0f, m_duration);
        }
    }
}

void BlackboardOverlay::render()
{
#ifndef SERVER_ONLY
    if (m_opacity <= 0.01f) return;

    // Only render for the local player whose kart has the board
    AbstractKart *kart = World::getWorld()->getKart(m_owner_kart_id);
    if (!kart || !kart->getController()->isLocalPlayerController())
        return;

    // Get screen dimensions
    irr::core::dimension2d<irr::u32> screen_size =
        irr_driver->getActualScreenSize();
    float sw = (float)screen_size.Width;
    float sh = (float)screen_size.Height;

    // Board dimensions: 75% screen coverage, landscape
    float board_w = sw * BOARD_COVERAGE * 1.4f;
    float board_h = sh * BOARD_COVERAGE;

    // Position: centered horizontally, slightly above center
    float cx = sw * 0.5f;
    float cy = sh * 0.5f + sh * m_board_y_offset;

    irr::video::IVideoDriver *driver = irr_driver->getVideoDriver();
    if (!driver) return;

    // Draw wood frame (outer rectangle)
    float frame = board_w * 0.04f;  // 4% border
    irr::core::rect<irr::s32> frame_rect(
        (irr::s32)(cx - board_w/2 - frame),
        (irr::s32)(cy - board_h/2 - frame),
        (irr::s32)(cx + board_w/2 + frame),
        (irr::s32)(cy + board_h/2 + frame)
    );
    irr::video::SColor wood_color(
        (irr::u32)(255 * m_opacity),
        115, 72, 30
    );
    driver->draw2DRectangle(wood_color, frame_rect);

    // Draw green board surface
    irr::core::rect<irr::s32> board_rect(
        (irr::s32)(cx - board_w/2),
        (irr::s32)(cy - board_h/2),
        (irr::s32)(cx + board_w/2),
        (irr::s32)(cy + board_h/2)
    );
    irr::video::SColor board_color(
        (irr::u32)(255 * m_opacity),
        28, 90, 45
    );
    driver->draw2DRectangle(board_color, board_rect);

    // Draw equations texture if available
    if (m_equations_texture)
    {
        irr::video::SColor eq_color(
            (irr::u32)(255 * m_opacity),
            255, 255, 255
        );
        irr::video::SColor colors[4] = { eq_color, eq_color, eq_color, eq_color };
        driver->draw2DImage(
            m_equations_texture,
            board_rect,
            irr::core::rect<irr::s32>(0, 0,
                m_equations_texture->getSize().Width,
                m_equations_texture->getSize().Height),
            nullptr,
            colors,
            true  // use alpha
        );
    }
#endif
}
