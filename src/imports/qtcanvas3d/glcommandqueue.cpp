/****************************************************************************
**
** Copyright (C) 2015 The Qt Company Ltd.
** Contact: http://www.qt.io/licensing/
**
** This file is part of the QtCanvas3D module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL3$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see http://www.qt.io/terms-conditions. For further
** information use the contact form at http://www.qt.io/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 3 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPLv3 included in the
** packaging of this file. Please review the following information to
** ensure the GNU Lesser General Public License version 3 requirements
** will be met: https://www.gnu.org/licenses/lgpl.html.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 2.0 or later as published by the Free
** Software Foundation and appearing in the file LICENSE.GPL included in
** the packaging of this file. Please review the following information to
** ensure the GNU General Public License version 2.0 requirements will be
** met: http://www.gnu.org/licenses/gpl-2.0.html.
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include "glcommandqueue_p.h"
#include "canvas3d_p.h" // for logging categories

#include <QtCore/QMap>
#include <QtCore/QMutexLocker>

QT_BEGIN_NAMESPACE
QT_CANVAS3D_BEGIN_NAMESPACE

/*!
 * \internal
 *
 * The CanvasGlCommandQueue is used to store OpenGL commands until we can execute them under
 * correct context in the renderer thread.
 *
 * The commands are all preallocated in a single block. The counter m_queuedCount indicates
 * how many commands from the beginning are actually used at any given time.
 *
 * The command data is copied for execution when GUI thread is blocked, so no extra synchronization
 * is needed for that.
 */
CanvasGlCommandQueue::CanvasGlCommandQueue(int size, QObject *parent) :
    QObject(parent),
    m_maxSize(0),
    m_queuedCount(0),
    m_nextResourceId(1),
    m_resourceIdOverflow(false),
    m_clearMask(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT)

{
    resetQueue(size);
}

CanvasGlCommandQueue::~CanvasGlCommandQueue()
{
    deleteUntransferedCommandData();

    // Note: Shader and program maps are cleared by canvas as correct context is required, so they
    // are not cleared here.
}

/*!
 * \internal
 *
 * Queues a new command \a id to the next available slot and returns a reference to that command,
 * so the caller can give it additional parameters.
 */
GlCommand &CanvasGlCommandQueue::queueCommand(CanvasGlCommandQueue::GlCommandId id)
{
    // If queue is full, we need to clear it first
    if (m_queuedCount == m_maxSize) {
        emit queueFull();
        // queueFull handling should reset the queue, but in case renderer thread is not available
        // to handle the commands for some reason, let's reset the count. In that case the entire
        // queue is lost, so results may not be pretty, but nothing much can be done about it.
        // Let's at least make sure we don't leak any memory.
        if (m_queuedCount) {
            deleteUntransferedCommandData();
            m_queuedCount = 0;
            clearQuickItemAsTextureList();
        }
    }

    GlCommand &command = m_queue[m_queuedCount++];
    command.id = id;
    command.data = 0;

    return command;
}

GlCommand &CanvasGlCommandQueue::queueCommand(CanvasGlCommandQueue::GlCommandId id, GLint p1,
                                              GLint p2, GLint p3, GLint p4, GLint p5, GLint p6,
                                              GLint p7, GLint p8)
{
    GlCommand &command = queueCommand(id);
    command.i1 = p1;
    command.i2 = p2;
    command.i3 = p3;
    command.i4 = p4;
    command.i5 = p5;
    command.i6 = p6;
    command.i7 = p7;
    command.i8 = p8;

    return command;
}

GlCommand &CanvasGlCommandQueue::queueCommand(CanvasGlCommandQueue::GlCommandId id, GLfloat p1,
                                              GLfloat p2, GLfloat p3, GLfloat p4)
{
    GlCommand &command = queueCommand(id);
    command.f1 = p1;
    command.f2 = p2;
    command.f3 = p3;
    command.f4 = p4;

    return command;
}

GlCommand &CanvasGlCommandQueue::queueCommand(CanvasGlCommandQueue::GlCommandId id, GLint i1,
                                              GLfloat p1, GLfloat p2, GLfloat p3, GLfloat p4)
{
    GlCommand &command = queueCommand(id);
    command.i1 = i1;
    command.f1 = p1;
    command.f2 = p2;
    command.f3 = p3;
    command.f4 = p4;

    return command;
}

GlCommand &CanvasGlCommandQueue::queueCommand(CanvasGlCommandQueue::GlCommandId id, GLint i1,
                                              GLint i2, GLfloat p1, GLfloat p2, GLfloat p3,
                                              GLfloat p4)
{
    GlCommand &command = queueCommand(id);
    command.i1 = i1;
    command.i2 = i2;
    command.f1 = p1;
    command.f2 = p2;
    command.f3 = p3;
    command.f4 = p4;

    return command;
}

/*!
 * \internal
 *
 * Copies command data to execute queue. GUI thread must be locked when this
 * method is called.
 */
void CanvasGlCommandQueue::transferCommands(QVector<GlCommand> &executeQueue)
{
    memcpy(executeQueue.data(), m_queue.data(), m_queuedCount * sizeof(GlCommand));

    m_queuedCount = 0;

    // Grab texture providers from quick items and cache them
    const int quickItemCount = m_quickItemsAsTextureList.size();
    if (quickItemCount) {
        for (int i = 0; i < quickItemCount; i++) {
            const ItemAndId *itemAndId = m_quickItemsAsTextureList.at(i);
            if (!itemAndId->itemPtr.isNull()) {
                QQuickItem *quickItem = itemAndId->itemPtr.data();
                QSGTextureProvider *texProvider = quickItem->textureProvider();
                if (texProvider) {
                    // Make sure the old provider, if any, gets cleared up before inserting a new one
                    delete m_providerCache.take(itemAndId->id);
                    m_providerCache.insert(itemAndId->id,
                                           new ProviderCacheItem(texProvider, quickItem));
                    // Reset the mapped glId so it gets resolved at render time
                    setGlIdToMap(itemAndId->id, 0,
                                 CanvasGlCommandQueue::internalClearQuickItemAsTexture);
                } else {
                    qCWarning(canvas3drendering).nospace() << "CanvasGlCommandQueue::"
                                                           << __FUNCTION__
                                                           << ": The Quick item doesn't implement a texture provider: "
                                                           << quickItem;
                }
            }
        }
        clearQuickItemAsTextureList();
    }
}

/*!
 * \internal
 *
 * Resets the queue.
 */
void CanvasGlCommandQueue::resetQueue(int size)
{
    deleteUntransferedCommandData();
    clearQuickItemAsTextureList();

    m_queuedCount = 0;
    m_maxSize = size;

    m_queue.clear();
    m_queue.resize(m_maxSize);

    m_resourceIdOverflow = false;
    m_nextResourceId = 1;

    // The reset should only be called when there is no valid context, so it should be safe
    // to assume any existing OpenGL resources, shaders, and programs have been deleted.
    // If they haven't been, we can't do it here anyway, since we don't have correct context.
    Q_ASSERT(!m_resourceIdMap.count());
    Q_ASSERT(!m_shaderMap.count());
    Q_ASSERT(!m_programMap.count());
}

/*!
 * \internal
 *
 * Deletes any extra data untransfered commands might have.
 */
void CanvasGlCommandQueue::deleteUntransferedCommandData()
{
    for (int i = 0; i < m_queuedCount; i++)
        m_queue[i].deleteData();
}

/*!
 * \internal
 *
 * Creates a new resource identifier for mapping an OpenGL resource identifier.
 */
GLint CanvasGlCommandQueue::createResourceId()
{
    QMutexLocker locker(&m_resourceMutex);

    GLint newId = m_nextResourceId++;

    // If the resource id overflowed, we need to check that we don't reallocate an id already in use
    if (m_resourceIdOverflow) {
        while (!newId || m_resourceIdMap.contains(newId))
            newId = m_nextResourceId++;
    }

    if (m_nextResourceId < 0) {
        m_resourceIdOverflow = true;
        m_nextResourceId = 1;
    }

    // Newly inserted ids always map a zero resource. This is updated when the actual id is created.
    m_resourceIdMap.insert(newId, GlResource());

    return newId;
}

void CanvasGlCommandQueue::setGlIdToMap(GLint id, GLuint glId, GlCommandId commandId)
{
    QMutexLocker locker(&m_resourceMutex);

    m_resourceIdMap.insert(id, GlResource(glId, commandId));
}

void CanvasGlCommandQueue::removeResourceIdFromMap(GLint id)
{
    QMutexLocker locker(&m_resourceMutex);

    m_resourceIdMap.remove(id);
}

GLuint CanvasGlCommandQueue::getGlId(GLint id)
{
    if (!id)
        return 0;

    QMutexLocker locker(&m_resourceMutex);

    return m_resourceIdMap.value(id).glId;
}

GLint CanvasGlCommandQueue::getCanvasId(GLuint glId, GlCommandId type)
{
    if (!glId)
        return 0;

    QMutexLocker locker(&m_resourceMutex);

    QList<GLint> keyList = m_resourceIdMap.keys();
    foreach (GLint canvasId, keyList) {
        GlResource value = m_resourceIdMap.value(canvasId);
        if (value.glId == glId && value.commandId == type)
            return canvasId;
    }

    return 0;
}

void CanvasGlCommandQueue::setShaderToMap(GLint id, QOpenGLShader *shader)
{
    QMutexLocker locker(&m_resourceMutex);

    m_shaderMap.insert(id, shader);
}

void CanvasGlCommandQueue::setProgramToMap(GLint id, QOpenGLShaderProgram *program)
{
    QMutexLocker locker(&m_resourceMutex);

    m_programMap.insert(id, program);
}

QOpenGLShader *CanvasGlCommandQueue::takeShaderFromMap(GLint id)
{
    if (!id)
        return 0;

    QMutexLocker locker(&m_resourceMutex);

    return m_shaderMap.take(id);
}

QOpenGLShaderProgram *CanvasGlCommandQueue::takeProgramFromMap(GLint id)
{
    if (!id)
        return 0;

    QMutexLocker locker(&m_resourceMutex);

    return m_programMap.take(id);
}

QOpenGLShader *CanvasGlCommandQueue::getShader(GLint id)
{
    if (!id)
        return 0;

    QMutexLocker locker(&m_resourceMutex);

    return m_shaderMap.value(id);
}

QOpenGLShaderProgram *CanvasGlCommandQueue::getProgram(GLint id)
{
    if (!id)
        return 0;

    QMutexLocker locker(&m_resourceMutex);

    return m_programMap.value(id);
}

GLuint CanvasGlCommandQueue::takeSingleIdParam(const GlCommand &command)
{
    const GLuint glId = getGlId(command.i1);
    removeResourceIdFromMap(command.i1);
    return glId;
}

void CanvasGlCommandQueue::handleGenerateCommand(const GlCommand &command, GLuint glId)
{
    setGlIdToMap(command.i1, glId, command.id);
}

/*!
 * \internal
 * Adds a quick item to list of items that need to be converted to texture IDs on the
 * next command transfer.
 */
void CanvasGlCommandQueue::addQuickItemAsTexture(QQuickItem *quickItem, GLint textureId)
{
    m_quickItemsAsTextureList.append(new ItemAndId(quickItem, textureId));
}

void CanvasGlCommandQueue::clearQuickItemAsTextureList()
{
    qDeleteAll(m_quickItemsAsTextureList);
    m_quickItemsAsTextureList.clear();
}

void CanvasGlCommandQueue::removeFromClearMask(GLbitfield mask)
{
    m_clearMask &= ~mask;
}

GLbitfield CanvasGlCommandQueue::resetClearMask()
{
    GLbitfield returnMask = m_clearMask;
    m_clearMask = GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT;
    return returnMask;
}

QT_CANVAS3D_END_NAMESPACE
QT_END_NAMESPACE