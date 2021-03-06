//
//  Menu.cpp
//  hifi
//
//  Created by Stephen Birarda on 8/12/13.
//  Copyright (c) 2013 HighFidelity, Inc. All rights reserved.
//

#include <cstdlib>

#include <QMenuBar>
#include <QBoxLayout>
#include <QColorDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QLineEdit>
#include <QMainWindow>
#include <QStandardPaths>

#include "Application.h"
#include "fvupdater.h"
#include "PairingHandler.h"
#include "Menu.h"
#include "Util.h"

Menu* Menu::_instance = NULL;

Menu* Menu::getInstance() {
    if (!_instance) {
        qDebug("First call to Menu::getInstance() - initing menu.\n");
        
        _instance = new Menu();
    }
        
    return _instance;
}

const ViewFrustumOffset DEFAULT_FRUSTUM_OFFSET = {-135.0f, 0.0f, 0.0f, 25.0f, 0.0f};

Menu::Menu() :
    _actionHash(),
    _audioJitterBufferSamples(0),
    _bandwidthDialog(NULL),
    _fieldOfView(DEFAULT_FIELD_OF_VIEW_DEGREES),
    _frustumDrawMode(FRUSTUM_DRAW_MODE_ALL),
    _viewFrustumOffset(DEFAULT_FRUSTUM_OFFSET),
    _voxelModeActionsGroup(NULL),
    _voxelStatsDialog(NULL)
{
    Application *appInstance = Application::getInstance();
    
    QMenu* fileMenu = addMenu("File");
   
    (addActionToQMenuAndActionHash(fileMenu,
                                   MenuOption::Preferences,
                                   Qt::CTRL | Qt::Key_Comma,
                                   this,
                                   SLOT(editPreferences())))->setMenuRole(QAction::PreferencesRole);
    
#if defined(Q_OS_MAC) && defined(QT_NO_DEBUG)
    // show "Check for Updates" in the menu
    (addActionToQMenuAndActionHash(fileMenu,
                                   MenuOption::CheckForUpdates,
                                   0,
                                   this,
                                   SLOT(checkForUpdates())))->setMenuRole(QAction::ApplicationSpecificRole);
#endif
    
    addDisabledActionAndSeparator(fileMenu, "Voxels");
    addActionToQMenuAndActionHash(fileMenu, MenuOption::ExportVoxels, Qt::CTRL | Qt::Key_E, appInstance, SLOT(exportVoxels()));
    addActionToQMenuAndActionHash(fileMenu, MenuOption::ImportVoxels, Qt::CTRL | Qt::Key_I, appInstance, SLOT(importVoxels()));
    
    addDisabledActionAndSeparator(fileMenu, "Go");
    addActionToQMenuAndActionHash(fileMenu,
                                  MenuOption::GoHome,
                                  Qt::CTRL | Qt::Key_G,
                                  appInstance->getAvatar(),
                                  SLOT(goHome()));
    addActionToQMenuAndActionHash(fileMenu,
                                  MenuOption::GoToDomain,
                                  Qt::CTRL | Qt::Key_D,
                                   this,
                                   SLOT(goToDomain()));
    addActionToQMenuAndActionHash(fileMenu,
                                  MenuOption::GoToLocation,
                                  Qt::CTRL | Qt::SHIFT | Qt::Key_L,
                                   this,
                                   SLOT(goToLocation()));

    
    addDisabledActionAndSeparator(fileMenu, "Settings");
    addActionToQMenuAndActionHash(fileMenu, MenuOption::SettingsImport, 0, this, SLOT(importSettings()));
    addActionToQMenuAndActionHash(fileMenu, MenuOption::SettingsExport, 0, this, SLOT(exportSettings()));
    
    addDisabledActionAndSeparator(fileMenu, "Devices");
    addActionToQMenuAndActionHash(fileMenu, MenuOption::Pair, 0, PairingHandler::getInstance(), SLOT(sendPairRequest()));
    addCheckableActionToQMenuAndActionHash(fileMenu, MenuOption::TransmitterDrive, 0, true);
    
    (addActionToQMenuAndActionHash(fileMenu,
                                   MenuOption::Quit,
                                   Qt::CTRL | Qt::Key_Q,
                                   appInstance,
                                   SLOT(quit())))->setMenuRole(QAction::QuitRole);    
    
    QMenu* editMenu = addMenu("Edit");
    addActionToQMenuAndActionHash(editMenu, MenuOption::CutVoxels, Qt::CTRL | Qt::Key_X, appInstance, SLOT(cutVoxels()));
    addActionToQMenuAndActionHash(editMenu, MenuOption::CopyVoxels, Qt::CTRL | Qt::Key_C, appInstance, SLOT(copyVoxels()));
    addActionToQMenuAndActionHash(editMenu, MenuOption::PasteVoxels, Qt::CTRL | Qt::Key_V, appInstance, SLOT(pasteVoxels()));
    addActionToQMenuAndActionHash(editMenu, MenuOption::NudgeVoxels, Qt::CTRL | Qt::Key_N, appInstance, SLOT(nudgeVoxels()));
    
    #ifdef __APPLE__
        addActionToQMenuAndActionHash(editMenu, MenuOption::DeleteVoxels, Qt::Key_Backspace, appInstance, SLOT(deleteVoxels()));
    #else
        addActionToQMenuAndActionHash(editMenu, MenuOption::DeleteVoxels, Qt::Key_Delete, appInstance, SLOT(deleteVoxels()));
    #endif
    
    addDisabledActionAndSeparator(editMenu, "Physics");
    addCheckableActionToQMenuAndActionHash(editMenu, MenuOption::Gravity, Qt::SHIFT | Qt::Key_G, true);
    addCheckableActionToQMenuAndActionHash(editMenu,
                                           MenuOption::Collisions,
                                           0,
                                           true,
                                           appInstance->getAvatar(),
                                           SLOT(setWantCollisionsOn(bool)));
    
    QMenu* toolsMenu = addMenu("Tools");
    
    _voxelModeActionsGroup = new QActionGroup(this);
    _voxelModeActionsGroup->setExclusive(false);
    
    QAction* addVoxelMode = addCheckableActionToQMenuAndActionHash(toolsMenu, MenuOption::VoxelAddMode, Qt::Key_V);
    _voxelModeActionsGroup->addAction(addVoxelMode);
    
    QAction* deleteVoxelMode = addCheckableActionToQMenuAndActionHash(toolsMenu, MenuOption::VoxelDeleteMode, Qt::Key_R);
    _voxelModeActionsGroup->addAction(deleteVoxelMode);
    
    QAction* colorVoxelMode = addCheckableActionToQMenuAndActionHash(toolsMenu, MenuOption::VoxelColorMode, Qt::Key_B);
    _voxelModeActionsGroup->addAction(colorVoxelMode);
    
    QAction* selectVoxelMode = addCheckableActionToQMenuAndActionHash(toolsMenu, MenuOption::VoxelSelectMode, Qt::Key_O);
    _voxelModeActionsGroup->addAction(selectVoxelMode);
    
    QAction* getColorMode = addCheckableActionToQMenuAndActionHash(toolsMenu, MenuOption::VoxelGetColorMode, Qt::Key_G);
    _voxelModeActionsGroup->addAction(getColorMode);
    
    // connect each of the voxel mode actions to the updateVoxelModeActionsSlot
    foreach (QAction* action, _voxelModeActionsGroup->actions()) {
        connect(action, SIGNAL(triggered()), this, SLOT(updateVoxelModeActions()));
    }
    
    QAction* voxelPaintColor = addActionToQMenuAndActionHash(toolsMenu,
                                                             MenuOption::VoxelPaintColor,
                                                             Qt::META | Qt::Key_C,
                                                             this,
                                                             SLOT(chooseVoxelPaintColor()));
    
    Application::getInstance()->getSwatch()->setAction(voxelPaintColor);
    
    QColor paintColor(128, 128, 128);
    voxelPaintColor->setData(paintColor);
    voxelPaintColor->setIcon(Swatch::createIcon(paintColor));
    
    addActionToQMenuAndActionHash(toolsMenu,
                                  MenuOption::DecreaseVoxelSize,
                                  QKeySequence::ZoomOut,
                                  appInstance,
                                  SLOT(decreaseVoxelSize()));
    addActionToQMenuAndActionHash(toolsMenu,
                                  MenuOption::IncreaseVoxelSize,
                                  QKeySequence::ZoomIn,
                                  appInstance,
                                  SLOT(increaseVoxelSize()));
    addActionToQMenuAndActionHash(toolsMenu, MenuOption::ResetSwatchColors, 0, this, SLOT(resetSwatchColors()));

    
    QMenu* viewMenu = addMenu("View");
    
    addCheckableActionToQMenuAndActionHash(viewMenu,
                                           MenuOption::Fullscreen,
                                           Qt::Key_F,
                                           false,
                                           appInstance,
                                           SLOT(setFullscreen(bool)));
    addCheckableActionToQMenuAndActionHash(viewMenu, MenuOption::FirstPerson, Qt::Key_P, true);
    
    addActionToQMenuAndActionHash(viewMenu,
                                  MenuOption::IncreaseAvatarSize,
                                  Qt::Key_Plus,
                                  appInstance->getAvatar(),
                                  SLOT(increaseSize()));
    addActionToQMenuAndActionHash(viewMenu,
                                  MenuOption::DecreaseAvatarSize,
                                  Qt::Key_Minus,
                                  appInstance->getAvatar(),
                                  SLOT(decreaseSize()));
    addActionToQMenuAndActionHash(viewMenu,
                                  MenuOption::ResetAvatarSize,
                                  0,
                                  appInstance->getAvatar(),
                                  SLOT(resetSize()));
    
    
    addCheckableActionToQMenuAndActionHash(viewMenu, MenuOption::Mirror, Qt::Key_H);
    
    addCheckableActionToQMenuAndActionHash(viewMenu,
                                           MenuOption::SkeletonTracking,
                                           0,
                                           false,
                                           appInstance->getWebcam(),
                                           SLOT(setSkeletonTrackingOn(bool)));
    
    addCheckableActionToQMenuAndActionHash(viewMenu,
                                           MenuOption::LEDTracking,
                                           0,
                                           false,
                                           appInstance->getWebcam()->getGrabber(),
                                           SLOT(setLEDTrackingOn(bool)));
    
    addCheckableActionToQMenuAndActionHash(viewMenu,
                                           MenuOption::OffAxisProjection,
                                           0,
                                           false);
                                           
    addDisabledActionAndSeparator(viewMenu, "Stats");
    addCheckableActionToQMenuAndActionHash(viewMenu, MenuOption::Stats, Qt::Key_Slash);
    addCheckableActionToQMenuAndActionHash(viewMenu, MenuOption::Log, Qt::CTRL | Qt::Key_L);
    addCheckableActionToQMenuAndActionHash(viewMenu, MenuOption::Oscilloscope, 0, true);
    addCheckableActionToQMenuAndActionHash(viewMenu, MenuOption::Bandwidth, 0, true);
    addActionToQMenuAndActionHash(viewMenu, MenuOption::BandwidthDetails, 0, this, SLOT(bandwidthDetails()));
    addActionToQMenuAndActionHash(viewMenu, MenuOption::VoxelStats, 0, this, SLOT(voxelStatsDetails()));
     
    QMenu* developerMenu = addMenu("Developer");
    addDisabledActionAndSeparator(developerMenu, "Rendering");
    
    addCheckableActionToQMenuAndActionHash(developerMenu,
                                           MenuOption::Voxels,
                                           Qt::SHIFT | Qt::Key_V,
                                           true,
                                           appInstance,
                                           SLOT(setRenderVoxels(bool)));
    addCheckableActionToQMenuAndActionHash(developerMenu, MenuOption::VoxelTextures);
    addCheckableActionToQMenuAndActionHash(developerMenu, MenuOption::AmbientOcclusion);
    addCheckableActionToQMenuAndActionHash(developerMenu, MenuOption::Stars, 0, true);
    addCheckableActionToQMenuAndActionHash(developerMenu, MenuOption::Atmosphere, Qt::SHIFT | Qt::Key_A, true);
    addCheckableActionToQMenuAndActionHash(developerMenu, MenuOption::GroundPlane, 0, true);
    addCheckableActionToQMenuAndActionHash(developerMenu, MenuOption::Avatars, 0, true);
    addCheckableActionToQMenuAndActionHash(developerMenu, MenuOption::AvatarAsBalls);
    
    addActionToQMenuAndActionHash(developerMenu,
                                  MenuOption::VoxelMode,
                                  0,
                                  appInstance->getAvatar()->getVoxels(),
                                  SLOT(cycleMode()));
    
    addActionToQMenuAndActionHash(developerMenu,
                                  MenuOption::FaceMode,
                                  0,
                                  &appInstance->getAvatar()->getHead().getFace(),
                                  SLOT(cycleRenderMode()));
    
    addActionToQMenuAndActionHash(developerMenu,
                                  MenuOption::GlowMode,
                                  0,
                                  appInstance->getGlowEffect(),
                                  SLOT(cycleRenderMode()));
    
    
    addCheckableActionToQMenuAndActionHash(developerMenu, MenuOption::LookAtVectors, 0, true);
    addCheckableActionToQMenuAndActionHash(developerMenu, MenuOption::LookAtIndicator, 0, true);
    addCheckableActionToQMenuAndActionHash(developerMenu, MenuOption::FrameTimer);
    
    addDisabledActionAndSeparator(developerMenu, "Testing");
    
    QMenu* frustumMenu = developerMenu->addMenu("View Frustum Debugging Tools");
    addCheckableActionToQMenuAndActionHash(frustumMenu, MenuOption::DisplayFrustum, Qt::SHIFT | Qt::Key_F);
    
    addActionToQMenuAndActionHash(frustumMenu,
                                  MenuOption::FrustumRenderMode,
                                  Qt::SHIFT | Qt::Key_R,
                                  this,
                                  SLOT(cycleFrustumRenderMode()));
    updateFrustumRenderModeAction();
    
    addActionToQMenuAndActionHash(developerMenu, MenuOption::RunTimingTests, 0, this, SLOT(runTests()));
    addActionToQMenuAndActionHash(developerMenu,
                                  MenuOption::TreeStats,
                                  Qt::SHIFT | Qt::Key_S,
                                  appInstance->getVoxels(),
                                  SLOT(collectStatsForTreesAndVBOs()));
    
    QMenu* renderDebugMenu = developerMenu->addMenu("Render Debugging Tools");
    addCheckableActionToQMenuAndActionHash(renderDebugMenu, MenuOption::PipelineWarnings);
    
    addActionToQMenuAndActionHash(renderDebugMenu,
                                  MenuOption::KillLocalVoxels,
                                  Qt::CTRL | Qt::Key_K,
                                  appInstance, SLOT(doKillLocalVoxels()));
    
    addActionToQMenuAndActionHash(renderDebugMenu,
                                  MenuOption::RandomizeVoxelColors,
                                  Qt::CTRL | Qt::Key_R,
                                  appInstance->getVoxels(),
                                  SLOT(randomizeVoxelColors()));
    
    addActionToQMenuAndActionHash(renderDebugMenu,
                                  MenuOption::FalseColorRandomly,
                                  0,
                                  appInstance->getVoxels(),
                                  SLOT(falseColorizeRandom()));
    
    addActionToQMenuAndActionHash(renderDebugMenu,
                                  MenuOption::FalseColorEveryOtherVoxel,
                                  0,
                                  appInstance->getVoxels(),
                                  SLOT(falseColorizeRandomEveryOther()));
    
    addActionToQMenuAndActionHash(renderDebugMenu,
                                  MenuOption::FalseColorByDistance,
                                  0,
                                  appInstance->getVoxels(),
                                  SLOT(falseColorizeDistanceFromView()));
    
    addActionToQMenuAndActionHash(renderDebugMenu,
                                  MenuOption::FalseColorOutOfView,
                                  0,
                                  appInstance->getVoxels(),
                                  SLOT(falseColorizeInView()));
    
    addActionToQMenuAndActionHash(renderDebugMenu,
                                  MenuOption::FalseColorOccluded,
                                  0,
                                  appInstance->getVoxels(),
                                  SLOT(falseColorizeOccluded()));
    
    addActionToQMenuAndActionHash(renderDebugMenu,
                                  MenuOption::FalseColorOccludedV2,
                                  0,
                                  appInstance->getVoxels(),
                                  SLOT(falseColorizeOccludedV2()));
    
    addActionToQMenuAndActionHash(renderDebugMenu,
                                  MenuOption::FalseColorBySource,
                                  0,
                                  appInstance->getVoxels(),
                                  SLOT(falseColorizeBySource()));
    
    addActionToQMenuAndActionHash(renderDebugMenu,
                                  MenuOption::ShowTrueColors,
                                  Qt::CTRL | Qt::Key_T,
                                  appInstance->getVoxels(),
                                  SLOT(trueColorize()));

    
    addCheckableActionToQMenuAndActionHash(developerMenu,
                                           MenuOption::Webcam,
                                           0,
                                           false,
                                           appInstance->getWebcam(),
                                           SLOT(setEnabled(bool)));

    addActionToQMenuAndActionHash(developerMenu,
                                  MenuOption::WebcamMode,
                                  0,
                                  appInstance->getWebcam()->getGrabber(),
                                  SLOT(cycleVideoSendMode()));
    addCheckableActionToQMenuAndActionHash(developerMenu,
                                           MenuOption::WebcamTexture,
                                           0,
                                           false,
                                           appInstance->getWebcam()->getGrabber(),
                                           SLOT(setDepthOnly(bool)));
    
    addCheckableActionToQMenuAndActionHash(developerMenu,
                                           MenuOption::FaceshiftTCP,
                                           0,
                                           false,
                                           appInstance->getFaceshift(),
                                           SLOT(setTCPEnabled(bool)));
                                           
    QMenu* audioDebugMenu = developerMenu->addMenu("Audio Debugging Tools");
    addCheckableActionToQMenuAndActionHash(audioDebugMenu, MenuOption::EchoAudio);
    addActionToQMenuAndActionHash(audioDebugMenu,
                                  MenuOption::ListenModeNormal,
                                  Qt::CTRL | Qt::Key_1,
                                  appInstance,
                                  SLOT(setListenModeNormal()));
    addActionToQMenuAndActionHash(audioDebugMenu,
                                  MenuOption::ListenModePoint,
                                  Qt::CTRL | Qt::Key_2,
                                  appInstance,
                                  SLOT(setListenModePoint()));
    addActionToQMenuAndActionHash(audioDebugMenu,
                                  MenuOption::ListenModeSingleSource,
                                  Qt::CTRL | Qt::Key_3,
                                  appInstance,
                                  SLOT(setListenModeSingleSource()));
    
    addCheckableActionToQMenuAndActionHash(developerMenu, MenuOption::TestPing, 0, true);
    
    addCheckableActionToQMenuAndActionHash(developerMenu,
                                           MenuOption::SendVoxelColors,
                                           0,
                                           true,
                                           appInstance->getAvatar(),
                                           SLOT(setWantColor(bool)));
    
    addCheckableActionToQMenuAndActionHash(developerMenu,
                                           MenuOption::LowRes,
                                           0,
                                           true,
                                           appInstance->getAvatar(),
                                           SLOT(setWantLowResMoving(bool)));
    
    addCheckableActionToQMenuAndActionHash(developerMenu,
                                           MenuOption::DeltaSending,
                                           0,
                                           true,
                                           appInstance->getAvatar(),
                                           SLOT(setWantDelta(bool)));
    
    addCheckableActionToQMenuAndActionHash(developerMenu,
                                           MenuOption::OcclusionCulling,
                                           Qt::SHIFT | Qt::Key_C,
                                           true,
                                           appInstance->getAvatar(),
                                           SLOT(setWantOcclusionCulling(bool)));
    
    addCheckableActionToQMenuAndActionHash(developerMenu, MenuOption::CoverageMap, Qt::SHIFT | Qt::CTRL | Qt::Key_O);
    addCheckableActionToQMenuAndActionHash(developerMenu, MenuOption::CoverageMapV2, Qt::SHIFT | Qt::CTRL | Qt::Key_P);
    addCheckableActionToQMenuAndActionHash(developerMenu, MenuOption::SimulateLeapHand);
    addCheckableActionToQMenuAndActionHash(developerMenu, MenuOption::TestRaveGlove);
    
    addCheckableActionToQMenuAndActionHash(developerMenu, MenuOption::GyroLook, 0, true);
    addCheckableActionToQMenuAndActionHash(developerMenu, MenuOption::HeadMouse);
    
    addDisabledActionAndSeparator(developerMenu, "Voxels");
    addCheckableActionToQMenuAndActionHash(developerMenu, MenuOption::DestructiveAddVoxel);
}

Menu::~Menu() {
    bandwidthDetailsClosed();
    voxelStatsDetailsClosed();
}

void Menu::loadSettings(QSettings* settings) {
    if (!settings) {
        settings = Application::getInstance()->getSettings();
    }
    
    _gyroCameraSensitivity = loadSetting(settings, "gyroCameraSensitivity", 0.5f);
    _audioJitterBufferSamples = loadSetting(settings, "audioJitterBufferSamples", 0);
    _fieldOfView = loadSetting(settings, "fieldOfView", DEFAULT_FIELD_OF_VIEW_DEGREES);
    
    settings->beginGroup("View Frustum Offset Camera");
    // in case settings is corrupt or missing loadSetting() will check for NaN
    _viewFrustumOffset.yaw = loadSetting(settings, "viewFrustumOffsetYaw", 0.0f);
    _viewFrustumOffset.pitch = loadSetting(settings, "viewFrustumOffsetPitch", 0.0f);
    _viewFrustumOffset.roll = loadSetting(settings, "viewFrustumOffsetRoll", 0.0f);
    _viewFrustumOffset.distance = loadSetting(settings, "viewFrustumOffsetDistance", 0.0f);
    _viewFrustumOffset.up = loadSetting(settings, "viewFrustumOffsetUp", 0.0f);
    settings->endGroup();
    
    scanMenuBar(&loadAction, settings);
    Application::getInstance()->getAvatar()->loadData(settings);
    Application::getInstance()->getSwatch()->loadData(settings);
}

void Menu::saveSettings(QSettings* settings) {
    if (!settings) {
        settings = Application::getInstance()->getSettings();
    }
    
    settings->setValue("gyroCameraSensitivity", _gyroCameraSensitivity);
    settings->setValue("audioJitterBufferSamples", _audioJitterBufferSamples);
    settings->setValue("fieldOfView", _fieldOfView);
    settings->beginGroup("View Frustum Offset Camera");
    settings->setValue("viewFrustumOffsetYaw", _viewFrustumOffset.yaw);
    settings->setValue("viewFrustumOffsetPitch", _viewFrustumOffset.pitch);
    settings->setValue("viewFrustumOffsetRoll", _viewFrustumOffset.roll);
    settings->setValue("viewFrustumOffsetDistance", _viewFrustumOffset.distance);
    settings->setValue("viewFrustumOffsetUp", _viewFrustumOffset.up);
    settings->endGroup();
    
    scanMenuBar(&saveAction, settings);
    Application::getInstance()->getAvatar()->saveData(settings);
    Application::getInstance()->getSwatch()->saveData(settings);
    
    // ask the NodeList to save its data
    NodeList::getInstance()->saveData(settings);
}

void Menu::importSettings() {
    QString locationDir(QStandardPaths::displayName(QStandardPaths::DesktopLocation));
    QString fileName = QFileDialog::getOpenFileName(Application::getInstance()->getWindow(),
                                                    tr("Open .ini config file"),
                                                    locationDir,
                                                    tr("Text files (*.ini)"));
    if (fileName != "") {
        QSettings tmp(fileName, QSettings::IniFormat);
        loadSettings(&tmp);
    }
}

void Menu::exportSettings() {
    QString locationDir(QStandardPaths::displayName(QStandardPaths::DesktopLocation));
    QString fileName = QFileDialog::getSaveFileName(Application::getInstance()->getWindow(),
                                                    tr("Save .ini config file"),
                                                    locationDir,
                                                    tr("Text files (*.ini)"));
    if (fileName != "") {
        QSettings tmp(fileName, QSettings::IniFormat);
        saveSettings(&tmp);
        tmp.sync();
    }
}

void Menu::checkForUpdates() {
#if defined(Q_OS_MAC) && defined(QT_NO_DEBUG)
    qDebug() << "Checking if there are available updates.\n";
    // if this is a release OS X build use fervor to check for an update
    FvUpdater::sharedUpdater()->SetFeedURL("http://s3.highfidelity.io/appcast.xml");
    FvUpdater::sharedUpdater()->CheckForUpdatesSilent();
#endif
}

void Menu::loadAction(QSettings* set, QAction* action) {
    if (action->isChecked() != set->value(action->text(), action->isChecked()).toBool()) {
        action->trigger();
    }
}

void Menu::saveAction(QSettings* set, QAction* action) {
    set->setValue(action->text(),  action->isChecked());
}

void Menu::scanMenuBar(settingsAction modifySetting, QSettings* set) {
    QList<QMenu*> menus = this->findChildren<QMenu *>();
    
    for (QList<QMenu *>::const_iterator it = menus.begin(); menus.end() != it; ++it) {
        scanMenu(*it, modifySetting, set);
    }
}

void Menu::scanMenu(QMenu* menu, settingsAction modifySetting, QSettings* set) {
    QList<QAction*> actions = menu->actions();
    
    set->beginGroup(menu->title());
    for (QList<QAction *>::const_iterator it = actions.begin(); actions.end() != it; ++it) {
        if ((*it)->menu()) {
            scanMenu((*it)->menu(), modifySetting, set);
        }
        if ((*it)->isCheckable()) {
            modifySetting(set, *it);
        }
    }
    set->endGroup();
}

void Menu::handleViewFrustumOffsetKeyModifier(int key) {
    const float VIEW_FRUSTUM_OFFSET_DELTA = 0.5f;
    const float VIEW_FRUSTUM_OFFSET_UP_DELTA = 0.05f;
    
    switch (key) {
        case Qt::Key_BracketLeft:
            _viewFrustumOffset.yaw -= VIEW_FRUSTUM_OFFSET_DELTA;
            break;
            
        case Qt::Key_BracketRight:
            _viewFrustumOffset.yaw += VIEW_FRUSTUM_OFFSET_DELTA;
            break;
            
        case Qt::Key_BraceLeft:
            _viewFrustumOffset.pitch -= VIEW_FRUSTUM_OFFSET_DELTA;
            break;
            
        case Qt::Key_BraceRight:
            _viewFrustumOffset.pitch += VIEW_FRUSTUM_OFFSET_DELTA;
            break;
            
        case Qt::Key_ParenLeft:
            _viewFrustumOffset.roll -= VIEW_FRUSTUM_OFFSET_DELTA;
            break;
            
        case Qt::Key_ParenRight:
            _viewFrustumOffset.roll += VIEW_FRUSTUM_OFFSET_DELTA;
            break;
            
        case Qt::Key_Less:
            _viewFrustumOffset.distance -= VIEW_FRUSTUM_OFFSET_DELTA;
            break;
            
        case Qt::Key_Greater:
            _viewFrustumOffset.distance += VIEW_FRUSTUM_OFFSET_DELTA;
            break;
            
        case Qt::Key_Comma:
            _viewFrustumOffset.up -= VIEW_FRUSTUM_OFFSET_UP_DELTA;
            break;
            
        case Qt::Key_Period:
            _viewFrustumOffset.up += VIEW_FRUSTUM_OFFSET_UP_DELTA;
            break;
            
        default:
            break;
    }
}

void Menu::addDisabledActionAndSeparator(QMenu* destinationMenu, const QString& actionName) {
    destinationMenu->addSeparator();
    (destinationMenu->addAction(actionName))->setEnabled(false);
}

QAction* Menu::addActionToQMenuAndActionHash(QMenu* destinationMenu,
                                             const QString actionName,
                                             const QKeySequence& shortcut,
                                             const QObject* receiver,
                                             const char* member) {
    QAction* action;
    
    if (receiver && member) {
        action = destinationMenu->addAction(actionName, receiver, member, shortcut);
    } else {
        action = destinationMenu->addAction(actionName);
        action->setShortcut(shortcut);
    }
    
    _actionHash.insert(actionName, action);
    
    return action;
}

QAction* Menu::addCheckableActionToQMenuAndActionHash(QMenu* destinationMenu,
                                                      const QString actionName,
                                                      const QKeySequence& shortcut,
                                                      const bool checked,
                                                      const QObject* receiver,
                                                      const char* member) {
    QAction* action = addActionToQMenuAndActionHash(destinationMenu, actionName, shortcut, receiver, member);
    action->setCheckable(true);
    action->setChecked(checked);
    
    return action;
}

bool Menu::isOptionChecked(const QString& menuOption) {
    return _actionHash.value(menuOption)->isChecked();
}

void Menu::triggerOption(const QString& menuOption) {
    _actionHash.value(menuOption)->trigger();
}

QAction* Menu::getActionForOption(const QString& menuOption) {
    return _actionHash.value(menuOption);
}

bool Menu::isVoxelModeActionChecked() {
    foreach (QAction* action, _voxelModeActionsGroup->actions()) {
        if (action->isChecked()) {
            return true;
        }
    }
    return false;
}

void Menu::editPreferences() {
    Application* applicationInstance = Application::getInstance();
    QDialog dialog(applicationInstance->getGLWidget());
    dialog.setWindowTitle("Interface Preferences");
    QBoxLayout* layout = new QBoxLayout(QBoxLayout::TopToBottom);
    dialog.setLayout(layout);
    
    QFormLayout* form = new QFormLayout();
    layout->addLayout(form, 1);
    
    const int QLINE_MINIMUM_WIDTH = 400;
    
    QLineEdit* domainServerHostname = new QLineEdit(QString(NodeList::getInstance()->getDomainHostname()));
    domainServerHostname->setMinimumWidth(QLINE_MINIMUM_WIDTH);
    form->addRow("Domain server:", domainServerHostname);
    
    QLineEdit* avatarURL = new QLineEdit(applicationInstance->getAvatar()->getVoxels()->getVoxelURL().toString());
    avatarURL->setMinimumWidth(QLINE_MINIMUM_WIDTH);
    form->addRow("Avatar URL:", avatarURL);
    
    QSpinBox* fieldOfView = new QSpinBox();
    fieldOfView->setMaximum(180);
    fieldOfView->setMinimum(1);
    fieldOfView->setValue(_fieldOfView);
    form->addRow("Vertical Field of View (Degrees):", fieldOfView);
    
    QDoubleSpinBox* gyroCameraSensitivity = new QDoubleSpinBox();
    gyroCameraSensitivity->setValue(_gyroCameraSensitivity);
    form->addRow("Gyro Camera Sensitivity (0 - 1):", gyroCameraSensitivity);
    
    QDoubleSpinBox* leanScale = new QDoubleSpinBox();
    leanScale->setValue(applicationInstance->getAvatar()->getLeanScale());
    form->addRow("Lean Scale:", leanScale);
    
    QSpinBox* audioJitterBufferSamples = new QSpinBox();
    audioJitterBufferSamples->setMaximum(10000);
    audioJitterBufferSamples->setMinimum(-10000);
    audioJitterBufferSamples->setValue(_audioJitterBufferSamples);
    form->addRow("Audio Jitter Buffer Samples (0 for automatic):", audioJitterBufferSamples);
    
    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    dialog.connect(buttons, SIGNAL(accepted()), SLOT(accept()));
    dialog.connect(buttons, SIGNAL(rejected()), SLOT(reject()));
    layout->addWidget(buttons);
    
    int ret = dialog.exec();
    applicationInstance->getWindow()->activateWindow();
    if (ret != QDialog::Accepted) {
         return;
     }
    
    QByteArray newHostname;
    
    if (domainServerHostname->text().size() > 0) {
        // the user input a new hostname, use that
        newHostname = domainServerHostname->text().toLocal8Bit();
    } else {
        // the user left the field blank, use the default hostname
        newHostname = QByteArray(DEFAULT_DOMAIN_HOSTNAME);
    }
    
    // check if the domain server hostname is new
    if (memcmp(NodeList::getInstance()->getDomainHostname(), newHostname.constData(), newHostname.size()) != 0) {
        
        NodeList::getInstance()->clear();
        
        // kill the local voxels
        applicationInstance->getVoxels()->killLocalVoxels();
        
        // reset the environment to default
        applicationInstance->getEnvironment()->resetToDefault();
        
        // set the new hostname
        NodeList::getInstance()->setDomainHostname(newHostname.constData());
    }
    
    QUrl url(avatarURL->text());
    applicationInstance->getAvatar()->getVoxels()->setVoxelURL(url);
    Avatar::sendAvatarVoxelURLMessage(url);
    
    _gyroCameraSensitivity = gyroCameraSensitivity->value();
    
    applicationInstance->getAvatar()->setLeanScale(leanScale->value());
    
    _audioJitterBufferSamples = audioJitterBufferSamples->value();
    
    if (_audioJitterBufferSamples != 0) {
        applicationInstance->getAudio()->setJitterBufferSamples(_audioJitterBufferSamples);
    }
    
    _fieldOfView = fieldOfView->value();
    applicationInstance->resizeGL(applicationInstance->getGLWidget()->width(), applicationInstance->getGLWidget()->height());
}

void Menu::goToDomain() {
    Application* applicationInstance = Application::getInstance();
    QDialog dialog(applicationInstance->getGLWidget());
    dialog.setWindowTitle("Go To Domain");
    QBoxLayout* layout = new QBoxLayout(QBoxLayout::TopToBottom);
    dialog.setLayout(layout);
    
    QFormLayout* form = new QFormLayout();
    layout->addLayout(form, 1);
    
    const int QLINE_MINIMUM_WIDTH = 400;
    
    QLineEdit* domainServerHostname = new QLineEdit(QString(NodeList::getInstance()->getDomainHostname()));
    domainServerHostname->setMinimumWidth(QLINE_MINIMUM_WIDTH);
    form->addRow("Domain server:", domainServerHostname);
    
    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    dialog.connect(buttons, SIGNAL(accepted()), SLOT(accept()));
    dialog.connect(buttons, SIGNAL(rejected()), SLOT(reject()));
    layout->addWidget(buttons);
    
    int ret = dialog.exec();
    applicationInstance->getWindow()->activateWindow();
    if (ret != QDialog::Accepted) {
         return;
     }
    
    QByteArray newHostname;
    
    if (domainServerHostname->text().size() > 0) {
        // the user input a new hostname, use that
        newHostname = domainServerHostname->text().toLocal8Bit();
    } else {
        // the user left the field blank, use the default hostname
        newHostname = QByteArray(DEFAULT_DOMAIN_HOSTNAME);
    }
    
    // check if the domain server hostname is new
    if (memcmp(NodeList::getInstance()->getDomainHostname(), newHostname.constData(), newHostname.size()) != 0) {
        
        NodeList::getInstance()->clear();
        
        // kill the local voxels
        applicationInstance->getVoxels()->killLocalVoxels();
        
        // reset the environment to default
        applicationInstance->getEnvironment()->resetToDefault();
        
        // set the new hostname
        NodeList::getInstance()->setDomainHostname(newHostname.constData());
    }
}

void Menu::goToLocation() {
    Application* applicationInstance = Application::getInstance();
    QDialog dialog(applicationInstance->getGLWidget());
    dialog.setWindowTitle("Go To Location");
    QBoxLayout* layout = new QBoxLayout(QBoxLayout::TopToBottom);
    dialog.setLayout(layout);
    
    QFormLayout* form = new QFormLayout();
    layout->addLayout(form, 1);
    
    const int QLINE_MINIMUM_WIDTH = 300;

    Application* appInstance = Application::getInstance();
    MyAvatar* myAvatar = appInstance->getAvatar();
    glm::vec3 avatarPos = myAvatar->getPosition();
    QString currentLocation = QString("%1, %2, %3").arg(QString::number(avatarPos.x), 
                QString::number(avatarPos.y), QString::number(avatarPos.z));

    QLineEdit* coordinates = new QLineEdit(currentLocation);
    coordinates->setMinimumWidth(QLINE_MINIMUM_WIDTH);
    form->addRow("Coordinates as x,y,z:", coordinates);
    
    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    dialog.connect(buttons, SIGNAL(accepted()), SLOT(accept()));
    dialog.connect(buttons, SIGNAL(rejected()), SLOT(reject()));
    layout->addWidget(buttons);
    
    int ret = dialog.exec();
    applicationInstance->getWindow()->activateWindow();
    if (ret != QDialog::Accepted) {
         return;
     }
    
    QByteArray newCoordinates;
    
    if (coordinates->text().size() > 0) {
        // the user input a new hostname, use that

        QString delimiterPattern(",");
        QStringList coordinateItems = coordinates->text().split(delimiterPattern);

        const int NUMBER_OF_COORDINATE_ITEMS = 3;
        const int X_ITEM = 0;
        const int Y_ITEM = 1;
        const int Z_ITEM = 2;
        if (coordinateItems.size() == NUMBER_OF_COORDINATE_ITEMS) {
            double x = coordinateItems[X_ITEM].toDouble();
            double y = coordinateItems[Y_ITEM].toDouble();
            double z = coordinateItems[Z_ITEM].toDouble();
            glm::vec3 newAvatarPos(x, y, z);
            
            if (newAvatarPos != avatarPos) {
                qDebug("Going To Location: %f, %f, %f...\n", x, y, z);
                myAvatar->setPosition(newAvatarPos);
            }
        }
    }
}


void Menu::bandwidthDetails() {
    
    if (! _bandwidthDialog) {
        _bandwidthDialog = new BandwidthDialog(Application::getInstance()->getGLWidget(),
                                               Application::getInstance()->getBandwidthMeter());
        connect(_bandwidthDialog, SIGNAL(closed()), SLOT(bandwidthDetailsClosed()));
        
        _bandwidthDialog->show();
    }
    _bandwidthDialog->raise();
}

void Menu::bandwidthDetailsClosed() {
    if (_bandwidthDialog) {
        delete _bandwidthDialog;
        _bandwidthDialog = NULL;
    }
}

void Menu::voxelStatsDetails() {
    if (!_voxelStatsDialog) {
        _voxelStatsDialog = new VoxelStatsDialog(Application::getInstance()->getGLWidget(),
                                                 Application::getInstance()->getVoxelSceneStats());
        connect(_voxelStatsDialog, SIGNAL(closed()), SLOT(voxelStatsDetailsClosed()));
        _voxelStatsDialog->show();
    }
    _voxelStatsDialog->raise();
}

void Menu::voxelStatsDetailsClosed() {
    if (_voxelStatsDialog) {
        delete _voxelStatsDialog;
        _voxelStatsDialog = NULL;
    }
}

void Menu::cycleFrustumRenderMode() {
    _frustumDrawMode = (FrustumDrawMode)((_frustumDrawMode + 1) % FRUSTUM_DRAW_MODE_COUNT);
    updateFrustumRenderModeAction();
}

void Menu::updateVoxelModeActions() {
    // only the sender can be checked
    foreach (QAction* action, _voxelModeActionsGroup->actions()) {
        if (action->isChecked() && action != sender()) {
            action->setChecked(false);
        }
    }
}

void Menu::chooseVoxelPaintColor() {
    Application* appInstance = Application::getInstance();
    QAction* paintColor = _actionHash.value(MenuOption::VoxelPaintColor);
    
    QColor selected = QColorDialog::getColor(paintColor->data().value<QColor>(),
                                             appInstance->getGLWidget(),
                                             "Voxel Paint Color");
    if (selected.isValid()) {
        paintColor->setData(selected);
        paintColor->setIcon(Swatch::createIcon(selected));
    }
    
    // restore the main window's active state
    appInstance->getWindow()->activateWindow();
}

void Menu::runTests() {
    runTimingTests();
}

void Menu::resetSwatchColors() {
    Application::getInstance()->getSwatch()->reset();
}

void Menu::updateFrustumRenderModeAction() {
    QAction* frustumRenderModeAction = _actionHash.value(MenuOption::FrustumRenderMode);
    switch (_frustumDrawMode) {
        default:
        case FRUSTUM_DRAW_MODE_ALL:
            frustumRenderModeAction->setText("Render Mode - All");
            break;
        case FRUSTUM_DRAW_MODE_VECTORS:
            frustumRenderModeAction->setText("Render Mode - Vectors");
            break;
        case FRUSTUM_DRAW_MODE_PLANES:
            frustumRenderModeAction->setText("Render Mode - Planes");
            break;
        case FRUSTUM_DRAW_MODE_NEAR_PLANE:
            frustumRenderModeAction->setText("Render Mode - Near");
            break;
        case FRUSTUM_DRAW_MODE_FAR_PLANE:
            frustumRenderModeAction->setText("Render Mode - Far");
            break;
        case FRUSTUM_DRAW_MODE_KEYHOLE:
            frustumRenderModeAction->setText("Render Mode - Keyhole");
            break;
    }
}
