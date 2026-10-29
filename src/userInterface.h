#ifndef SODIUM_USER_INTERFACE_H
#define SODIUM_USER_INTERFACE_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initializes the Sodium Dear ImGui user interface and applies the
 *        signature Royal Purple & Pure White theme.
 */
void SodiumUI_Init(void);

/**
 * @brief Renders the complete Sodium multi-workspace user interface frame.
 *        Should be called inside the main application render loop between
 *        ImGui::NewFrame() and ImGui::Render().
 */
void SodiumUI_RenderFrame(void);

/**
 * @brief Shuts down the Sodium UI controller and frees allocated state.
 */
void SodiumUI_Shutdown(void);

#ifdef __cplusplus
}
#endif

#endif // SODIUM_USER_INTERFACE_H
