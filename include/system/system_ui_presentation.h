/**
 * @file system_ui_presentation.h
 * @brief Profile composition-root presentation refresh entrypoint.
 */

#ifndef SYSTEM_SYSTEM_UI_PRESENTATION_H
#define SYSTEM_SYSTEM_UI_PRESENTATION_H

void refreshSystemUiPresentation();
void setPhysicalAuthorizationPreview(bool confirmed);
void setI2cReturnPreview(bool confirmed);

#endif  // SYSTEM_SYSTEM_UI_PRESENTATION_H
