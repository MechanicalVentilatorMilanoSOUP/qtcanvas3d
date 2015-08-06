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

#include "canvastextureprovider_p.h"
#include "texture3d_p.h"

#include <QtQuick/QQuickItem>

QT_BEGIN_NAMESPACE
QT_CANVAS3D_BEGIN_NAMESPACE

/*!
   \qmltype Canvas3DTextureProvider
   \since QtCanvas3D 1.1
   \inqmlmodule QtCanvas3D
   \brief Provides means to get QQuickItem as Canvas3DTexture.

   An uncreatable QML type that provides an extension API that can be used to get QQuickItem as
   Canvas3DTexture. Only QQuickItems that implement QQuickItem::textureProvider() method can be
   used as a texture source, which in most cases means the \c{layer.enabled} property of the
   item must be set to \c {true}.

   Typical usage would be something like this:
   \code
    // In QML code, declare a layered item you wish to show as texture
    Rectangle {
        id: textureSource
        layer.enabled: true
        // ...
    }
    .
    .
    // In JavaScript code, declare the variables for the extension and the texture
    var textureProvider;
    var myTexture;
    .
    .
    // Get the extension after the context has been created
    textureProvider = gl.getExtension("QTCANVAS3D_texture_provider");
    .
    .
    // Get the Canvas3DTexture object representing our source item
    if (textureProvider)
        myTexture = textureProvider.createTextureFromSource(textureSource);
    .
    .
    // Normally the above is enough. However, in cases where you utilize synchronous OpenGL
    // commands or dynamically enable the source item layer, it is not guaranteed that the texture
    // is valid immediately after calling createTextureFromSource().
    // To ensure you don't use the texture before it is ready, connect the textureReady() signal
    // to a handler function that will use the texture.
    textureProvider.textureReady.connect(function(sourceItem) {
        if (sourceItem === textureSource) {
            gl.bindTexture(gl.TEXTURE_2D, myTexture);
            // ...
        }
    });
    \endcode

   \sa Context3D
 */
CanvasTextureProvider::CanvasTextureProvider(CanvasContext *canvasContext,
                                             QObject *parent) :
    QObject(parent),
    m_canvasContext(canvasContext)
{
}

CanvasTextureProvider::~CanvasTextureProvider()
{
}

/*!
 * \qmlmethod QJSValue Canvas3DTextureProvider::createTextureFromSource(Item *source)
 *
 * Creates and returns a Canvas3DTexture object for the supplied \a source item.
 *
 * The \a source item must be of a type that implements a texture provider, which in most
 * cases means the \c{layer.enabled} property of the item must be set to \c {true}.
 * ShaderEffectSource items can also be used as texture sources.
 * The texture provider of the \a source item owns the OpenGL texture.
 * If the \a source item is deleted or the \c{layer.enabled} property is set to \c{false}
 * while the texture is still in use in Canvas3D, the rendered texture contents become undefined.
 *
 * Trying to bind the returned Canvas3DTexture object is not guaranteed to work until
 * a \l{Canvas3DTextureProvider::textureReady}{textureReady()} signal corresponding to the \a source item has been emitted.
 * However, if you don't have any synchronous OpenGL calls between the first use of the texture
 * and the end of your paingGL() handler, and if you can guarantee that the source item has been
 * fully rendered at least once after its layer was enabled, you can immediately use the returned
 * texture without waiting for the \l{Canvas3DTextureProvider::textureReady}{textureReady()} signal.
 *
 * Disabling the \a source item's layer will destroy the underlying texture provider, so it
 * is necessary to call this method again for the \a source item if you re-enable its layer.
 *
 * If this function is called twice for same \a source, it doesn't create a new Canvas3DTexture
 * instance, but instead returns a reference to a previously created one, as long as the previous
 * instance is still alive.
 */
QJSValue CanvasTextureProvider::createTextureFromSource(QQuickItem *source)
{
    return m_canvasContext->createTextureFromSource(source);
}

/*!
 * \qmlsignal void Canvas3DTextureProvider::textureReady(Item *source)
 *
 * Indicates that the texture created with createTextureFromSource() method for the \a source item
 * is ready to be used.
 */
QT_CANVAS3D_END_NAMESPACE
QT_END_NAMESPACE