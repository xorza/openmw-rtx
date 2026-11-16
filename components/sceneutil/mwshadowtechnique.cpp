// clang-format off
/* This file is based on OpenSceneGraph's src/osgShadow/ViewDependentShadowMap.cpp.
 * Where applicable, any changes made are covered by OpenMW's GPL 3 license, not the OSGPL.
 * The original copyright notice is listed below.
 */

/* -*-c++-*- OpenSceneGraph - Copyright (C) 1998-2011 Robert Osfield
 *
 * This library is open source and may be redistributed and/or modified under
 * the terms of the OpenSceneGraph Public License (OSGPL) version 0.0 or
 * (at your option) any later version.  The full license is in LICENSE file
 * included with this distribution, and on the openscenegraph.org website.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * OpenSceneGraph Public License for more details.
*/

#include "mwshadowtechnique.hpp"

#include <algorithm>
#include <cmath>
#include <osgShadow/ShadowedScene>
#include <osg/CullFace>
#include <osg/Geometry>
#include <osg/io_utils>
#include <osg/Depth>
#include <osg/ClipControl>

#include <sstream>
#include <vector>

#include "glextensions.hpp"
#include "shadowsbin.hpp"
#include "lightmanager.hpp"

// NOLINTBEGIN(readability-identifier-naming)

namespace {
using namespace osgShadow;
using namespace SceneUtil;

constexpr double sDoubleMost = std::numeric_limits<double>::max();

//////////////////////////////////////////////////////////////////
// fragment shader
//
static const char fragmentShaderSource_withBaseTexture[] =
        "uniform sampler2D baseTexture;                                          \n"
        "uniform int baseTextureUnit;                                            \n"
        "uniform sampler2DShadow shadowTexture0;                                 \n"
        "uniform int shadowTextureUnit0;                                         \n"
        "                                                                        \n"
        "void main(void)                                                         \n"
        "{                                                                       \n"
        "  vec4 colorAmbientEmissive = gl_FrontLightModelProduct.sceneColor;     \n"
        "  vec4 color = texture2D( baseTexture, gl_TexCoord[baseTextureUnit].xy );                                              \n"
        "  color *= mix( colorAmbientEmissive, gl_Color, shadow2DProj( shadowTexture0, gl_TexCoord[shadowTextureUnit0] ).r );     \n"
        "  gl_FragColor = color;                                                                                                \n"
        "} \n";

static const char fragmentShaderSource_withBaseTexture_twoShadowMaps[] =
        "uniform sampler2D baseTexture;                                          \n"
        "uniform int baseTextureUnit;                                            \n"
        "uniform sampler2DShadow shadowTexture0;                                 \n"
        "uniform int shadowTextureUnit0;                                         \n"
        "uniform sampler2DShadow shadowTexture1;                                 \n"
        "uniform int shadowTextureUnit1;                                         \n"
        "                                                                        \n"
        "void main(void)                                                         \n"
        "{                                                                       \n"
        "  vec4 colorAmbientEmissive = gl_FrontLightModelProduct.sceneColor;     \n"
        "  vec4 color = texture2D( baseTexture, gl_TexCoord[baseTextureUnit].xy );              \n"
        "  float shadow0 = shadow2DProj( shadowTexture0, gl_TexCoord[shadowTextureUnit0] ).r;   \n"
        "  float shadow1 = shadow2DProj( shadowTexture1, gl_TexCoord[shadowTextureUnit1] ).r;   \n"
        "  color *= mix( colorAmbientEmissive, gl_Color, shadow0*shadow1 );                     \n"
        "  gl_FragColor = color;                                                                \n"
        "} \n";

std::string debugVertexShaderSource = "void main(void){gl_Position = gl_Vertex; gl_TexCoord[0]=gl_MultiTexCoord0;}";
std::string debugFragmentShaderSource =
        "uniform sampler2D texture;                                              \n"
        "                                                                        \n"
        "void main(void)                                                         \n"
        "{                                                                       \n"
        "    float f = texture2D(texture, gl_TexCoord[0].xy).r;                  \n"
        "                                                                        \n"
        "    f = 256.0 * f;                                                      \n"
        "    float fC = floor( f ) / 256.0;                                      \n"
        "                                                                        \n"
        "    f = 256.0 * fract( f );                                             \n"
        "    float fS = floor( f ) / 256.0;                                      \n"
        "                                                                        \n"
        "    f = 256.0 * fract( f );                                             \n"
        "    float fH = floor( f ) / 256.0;                                      \n"
        "                                                                        \n"
        "    fS *= 0.5;                                                          \n"
        "    fH = ( fH  * 0.34 + 0.66 ) * ( 1.0 - fS );                          \n"
        "                                                                        \n"
        "    vec3 rgb = vec3( ( fC > 0.5 ? ( 1.0 - fC ) : fC ),                  \n"
        "                     abs( fC - 0.333333 ),                              \n"
        "                     abs( fC - 0.666667 ) );                            \n"
        "                                                                        \n"
        "    rgb = min( vec3( 1.0, 1.0, 1.0 ), 3.0 * rgb );                      \n"
        "                                                                        \n"
        "    float fMax = max( max( rgb.r, rgb.g ), rgb.b );                     \n"
        "    fMax = 1.0 / fMax;                                                  \n"
        "                                                                        \n"
        "    vec3 color = fMax * rgb;                                            \n"
        "                                                                        \n"
        "    gl_FragColor =  vec4( fS + fH * color, 1 );                         \n"
        "}                                                                       \n";

std::string debugFrustumVertexShaderSource = "varying float depth; uniform mat4 transform; void main(void){gl_Position = transform * gl_Vertex; depth = gl_Position.z / gl_Position.w;}";
std::string debugFrustumFragmentShaderSource =
        "varying float depth;                                                    \n"
        "                                                                        \n"
        "void main(void)                                                         \n"
        "{                                                                       \n"
        "    float f = depth;                                                    \n"
        "                                                                        \n"
        "    f = 256.0 * f;                                                      \n"
        "    float fC = floor( f ) / 256.0;                                      \n"
        "                                                                        \n"
        "    f = 256.0 * fract( f );                                             \n"
        "    float fS = floor( f ) / 256.0;                                      \n"
        "                                                                        \n"
        "    f = 256.0 * fract( f );                                             \n"
        "    float fH = floor( f ) / 256.0;                                      \n"
        "                                                                        \n"
        "    fS *= 0.5;                                                          \n"
        "    fH = ( fH  * 0.34 + 0.66 ) * ( 1.0 - fS );                          \n"
        "                                                                        \n"
        "    vec3 rgb = vec3( ( fC > 0.5 ? ( 1.0 - fC ) : fC ),                  \n"
        "                     abs( fC - 0.333333 ),                              \n"
        "                     abs( fC - 0.666667 ) );                            \n"
        "                                                                        \n"
        "    rgb = min( vec3( 1.0, 1.0, 1.0 ), 3.0 * rgb );                      \n"
        "                                                                        \n"
        "    float fMax = max( max( rgb.r, rgb.g ), rgb.b );                     \n"
        "    fMax = 1.0 / fMax;                                                  \n"
        "                                                                        \n"
        "    vec3 color = fMax * rgb;                                            \n"
        "                                                                        \n"
        "    gl_FragColor =  vec4( fS + fH * color, 1 );                         \n"
        "}                                                                       \n";


template<class T>
class RenderLeafTraverser : public T
{
public:

    RenderLeafTraverser()
    {
    }

    void traverse(const osgUtil::RenderStage* rs)
    {
        traverse(static_cast<const osgUtil::RenderBin*>(rs));
    }

    void traverse(const osgUtil::RenderBin* renderBin)
    {
        const osgUtil::RenderBin::RenderBinList& rbl = renderBin->getRenderBinList();
        for(const auto & itr : rbl)
        {
            traverse(itr.second.get());
        }

        const osgUtil::RenderBin::RenderLeafList& rll = renderBin->getRenderLeafList();
        for(auto itr : rll)
        {
            handle(itr);
        }

        const osgUtil::RenderBin::StateGraphList& rgl = renderBin->getStateGraphList();
        for(auto itr : rgl)
        {
            traverse(itr);
        }

    }

    void traverse(const osgUtil::StateGraph* stateGraph)
    {
        const osgUtil::StateGraph::ChildList& cl = stateGraph->_children;
        for(const auto & itr : cl)
        {
            traverse(itr.second.get());
        }

        const osgUtil::StateGraph::LeafList& ll = stateGraph->_leaves;
        for(const auto & itr : ll)
        {
            handle(itr.get());
        }
    }

    inline void handle(const osgUtil::RenderLeaf* renderLeaf)
    {
        this->operator()(renderLeaf);
    }
};

///////////////////////////////////////////////////////////////////////////////////////////////
//
// VDSMCameraCullCallback
//
class VDSMCameraCullCallback : public osg::NodeCallback
{
    public:

        VDSMCameraCullCallback(MWShadowTechnique* vdsm, osg::Polytope& polytope);

        void operator()(osg::Node*, osg::NodeVisitor* nv) override;

        osg::RefMatrix* getProjectionMatrix() { return _projectionMatrix.get(); }
        osgUtil::RenderStage* getRenderStage() { return _renderStage.get(); }

    protected:

        MWShadowTechnique*                      _vdsm;
        osg::ref_ptr<osg::RefMatrix>            _projectionMatrix;
        osg::ref_ptr<osgUtil::RenderStage>      _renderStage;
        osg::Polytope                           _polytope;
};

VDSMCameraCullCallback::VDSMCameraCullCallback(MWShadowTechnique* vdsm, osg::Polytope& polytope):
    _vdsm(vdsm),
    _polytope(polytope)
{
}

void VDSMCameraCullCallback::operator()(osg::Node* node, osg::NodeVisitor* nv)
{
    auto* cv = static_cast<osgUtil::CullVisitor*>(nv);
    osg::Camera* camera = node->asCamera();
    OSG_INFO<<"VDSMCameraCullCallback::operator()(osg::Node* "<<camera<<", osg::NodeVisitor* "<<cv<<")"<<std::endl;

    if (!_polytope.empty())
    {
        OSG_INFO<<"Pushing custom Polytope"<<std::endl;

        osg::CullingSet& cs = cv->getProjectionCullingStack().back();

        cs.setFrustum(_polytope);

        cv->pushCullingSet();
    }
    // bin has to go inside camera cull or the rendertexture stage will override it
    cv->pushStateSet(_vdsm->getOrCreateShadowsBinStateSet());
    if (_vdsm->getShadowedScene())
    {
        _vdsm->getShadowedScene()->osg::Group::traverse(*nv);
    }
    cv->popStateSet();
    if (!_polytope.empty())
    {
        OSG_INFO<<"Popping custom Polytope"<<std::endl;
        cv->popCullingSet();
    }

    _renderStage = cv->getCurrentRenderBin()->getStage();

    OSG_INFO<<"VDSM second : _renderStage = "<<_renderStage<<std::endl;

    if (cv->getComputeNearFarMode() != osg::CullSettings::DO_NOT_COMPUTE_NEAR_FAR)
    {
        // make sure that the near plane is computed correctly.
        cv->computeNearPlane();

        osg::Matrixd projection = *(cv->getProjectionMatrix());

        OSG_INFO<<"RTT Projection matrix "<<projection<<std::endl;

        osg::Matrix::value_type left = NAN, right = NAN, bottom = NAN, top = NAN, zNear = NAN, zFar = NAN;
        // The projection's own type, which is double whatever precision the build's OSG keeps an osg::Matrix in.
        osg::Matrixd::value_type epsilon = 1e-6;
        if (std::abs(projection(0,3))<epsilon  && std::abs(projection(1,3))<epsilon  && std::abs(projection(2,3))<epsilon )
        {
            projection.getOrtho(left, right,
                                bottom, top,
                                zNear,  zFar);

            OSG_INFO<<"Ortho zNear="<<zNear<<", zFar="<<zFar<<std::endl;
        }
        else
        {
            projection.getFrustum(left, right,
                                bottom, top,
                                zNear,  zFar);

            OSG_INFO<<"Frustum zNear="<<zNear<<", zFar="<<zFar<<std::endl;
        }

        OSG_INFO<<"Calculated zNear = "<<cv->getCalculatedNearPlane()<<", zFar = "<<cv->getCalculatedFarPlane()<<std::endl;

        zNear = osg::maximum(zNear, cv->getCalculatedNearPlane());
        zFar = osg::minimum(zFar, cv->getCalculatedFarPlane());

        cv->setCalculatedNearPlane(zNear);
        cv->setCalculatedFarPlane(zFar);

        cv->clampProjectionMatrix(projection, zNear, zFar);

        //OSG_INFO<<"RTT zNear = "<<zNear<<", zFar = "<<zFar<<std::endl;
        OSG_INFO<<"RTT Projection matrix after clamping "<<projection<<std::endl;

        camera->setProjectionMatrix(projection);
    }

    _projectionMatrix = cv->getProjectionMatrix();
}
} // namespace

MWShadowTechnique::ComputeLightSpaceBounds::ComputeLightSpaceBounds() :
    osg::NodeVisitor(osg::NodeVisitor::TRAVERSE_ACTIVE_CHILDREN)
{
    setCullingMode(osg::CullSettings::VIEW_FRUSTUM_CULLING);
}

void MWShadowTechnique::ComputeLightSpaceBounds::reset()
{
    osg::CullStack::reset();
    _bb = osg::BoundingBox();
}

void MWShadowTechnique::ComputeLightSpaceBounds::apply(osg::Node& node)
{
    if (isCulled(node)) return;

    // push the culling mode.
    pushCurrentMask();

    traverse(node);

    // pop the culling mode.
    popCurrentMask();
}

void MWShadowTechnique::ComputeLightSpaceBounds::apply(osg::Group& node)
{
    apply(static_cast<osg::Node&>(node));
}

void MWShadowTechnique::ComputeLightSpaceBounds::apply(osg::Drawable& drawable)
{
    if (isCulled(drawable)) return;

    // push the culling mode.
    pushCurrentMask();

    updateBound(drawable.getBoundingBox());

    // pop the culling mode.
    popCurrentMask();
}

void MWShadowTechnique::ComputeLightSpaceBounds::apply(osg::Geometry& drawable)
{
    apply(static_cast<osg::Drawable&>(drawable));
}

void MWShadowTechnique::ComputeLightSpaceBounds::apply(osg::Billboard&)
{
    OSG_INFO << "Warning Billboards not yet supported" << std::endl;
    return;
}

void MWShadowTechnique::ComputeLightSpaceBounds::apply(osg::Projection&)
{
    // projection nodes won't affect a shadow map so their subgraphs should be ignored
    return;
}

void MWShadowTechnique::ComputeLightSpaceBounds::apply(osg::Transform& transform)
{
    if (isCulled(transform)) return;

    // push the culling mode.
    pushCurrentMask();

    // absolute transforms won't affect a shadow map so their subgraphs should be ignored.
    if (transform.getReferenceFrame() == osg::Transform::RELATIVE_RF)
    {
        osg::RefMatrix* matrix = createOrReuseMatrix(*getModelViewMatrix());
        transform.computeLocalToWorldMatrix(*matrix, this);
        pushModelViewMatrix(matrix, transform.getReferenceFrame());

        traverse(transform);

        popModelViewMatrix();
    }

    // pop the culling mode.
    popCurrentMask();
}

void MWShadowTechnique::ComputeLightSpaceBounds::apply(osg::MatrixTransform& transform)
{
    apply(static_cast<osg::Transform&>(transform));
}

void MWShadowTechnique::ComputeLightSpaceBounds::apply(osg::Camera&)
{
    // camera nodes won't affect a shadow map so their subgraphs should be ignored
    return;
}

void MWShadowTechnique::ComputeLightSpaceBounds::updateBound(const osg::BoundingBox& bb)
{
    if (!bb.valid()) return;

    const osg::Matrix& matrix = *getModelViewMatrix() * *getProjectionMatrix();

    update(bb.corner(0) * matrix);
    update(bb.corner(1) * matrix);
    update(bb.corner(2) * matrix);
    update(bb.corner(3) * matrix);
    update(bb.corner(4) * matrix);
    update(bb.corner(5) * matrix);
    update(bb.corner(6) * matrix);
    update(bb.corner(7) * matrix);
}

void MWShadowTechnique::ComputeLightSpaceBounds::update(const osg::Vec3& v)
{
    if (v.z()<-1.0f)
    {
        //OSG_NOTICE<<"discarding("<<v<<")"<<std::endl;
        return;
    }
    float x = v.x();
    x = std::max(x, -1.0f);
    x = std::min(x, 1.0f);
    float y = v.y();
    y = std::max(y, -1.0f);
    y = std::min(y, 1.0f);
    _bb.expandBy(osg::Vec3(x, y, v.z()));
}

///////////////////////////////////////////////////////////////////////////////////////////////
//
// LightData
//
MWShadowTechnique::LightData::LightData(MWShadowTechnique::ViewDependentData* vdd):
    _viewDependentData(vdd),
    directionalLight(false)
{
}

void MWShadowTechnique::LightData::setLightData(osg::RefMatrix* lm, std::shared_ptr<const Light> l, const osg::Matrixd& modelViewMatrix)
{
    lightMatrix = lm;
    light = std::move(l);

    lightPos = light->getPosition();
    directionalLight = (light->getPosition().w()== 0.0f);
    if (directionalLight)
    {
        lightPos3.set(0.0, 0.0, 0.0); // directional light has no destinct position
        lightDir.set(-lightPos.x(), -lightPos.y(), -lightPos.z());
        lightDir.normalize();
        OSG_INFO<<"   Directional light, lightPos="<<lightPos<<", lightDir="<<lightDir<<std::endl;
        if (lightMatrix.valid() && lightMatrix->operator==(osg::Matrixf(modelViewMatrix)))
        {
            OSG_INFO<<"   Light matrix "<<*lightMatrix<<std::endl;
            osg::Matrix lightToLocalMatrix(*lightMatrix * osg::Matrix::inverse(modelViewMatrix) );
            lightDir = osg::Matrix::transform3x3( lightDir, lightToLocalMatrix );
            lightDir.normalize();
            OSG_INFO<<"   new LightDir ="<<lightDir<<std::endl;
        }
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////
//
// ShadowData
//
MWShadowTechnique::ShadowData::ShadowData(MWShadowTechnique::ViewDependentData* vdd):
    _viewDependentData(vdd),
    _textureUnit(0),
    _sm_i(0)
{

    const ShadowSettings* settings = vdd->getViewDependentShadowMap()->getShadowedScene()->getShadowSettings();

    bool debug = settings->getDebugDraw();

    // set up the texture
    _texture = new osg::Texture2D;

    osg::Vec2s textureSize = debug ? osg::Vec2s(512,512) : settings->getTextureSize();
    _texture->setTextureSize(textureSize.x(), textureSize.y());

    if (debug)
    {
        _texture->setInternalFormat(GL_RGB);
    }
    else
    {
        _texture->setInternalFormat(GL_DEPTH_COMPONENT);
        _texture->setShadowComparison(true);
        _texture->setShadowTextureMode(osg::Texture2D::LUMINANCE);
    }

    _texture->setFilter(osg::Texture2D::MIN_FILTER,osg::Texture2D::LINEAR);
    _texture->setFilter(osg::Texture2D::MAG_FILTER,osg::Texture2D::LINEAR);

    // the shader clips sampled coordinates, so no need for border
    _texture->setWrap(osg::Texture2D::WRAP_S,osg::Texture2D::CLAMP_TO_EDGE);
    _texture->setWrap(osg::Texture2D::WRAP_T,osg::Texture2D::CLAMP_TO_EDGE);

    // set up the camera
    _camera = new osg::Camera;
    _camera->setName("ShadowCamera");
    _camera->setReferenceFrame(osg::Camera::ABSOLUTE_RF_INHERIT_VIEWPOINT);
    _camera->setImplicitBufferAttachmentMask(0, 0);
    //_camera->setClearColor(osg::Vec4(1.0f,1.0f,1.0f,1.0f));
    _camera->setClearColor(osg::Vec4(0.0f,0.0f,0.0f,0.0f));

    //_camera->setComputeNearFarMode(osg::Camera::COMPUTE_NEAR_FAR_USING_BOUNDING_VOLUMES);
    //_camera->setComputeNearFarMode(osg::Camera::COMPUTE_NEAR_FAR_USING_PRIMITIVES);

    // Now we are using Depth Clamping, we want to not cull things on the wrong side of the near plane.
    // When the near and far planes are computed, OSG always culls anything on the wrong side of the near plane, even if it's told not to.
    // Even if that weren't an issue, the near plane can't go past any shadow receivers or the depth-clamped fragments which ended up on the near plane can't cast shadows on those receivers.
    // Unfortunately, this change will make shadows have less depth precision when there are no casters outside the view frustum.
    // TODO: Find a better solution. E.g. detect when there are no casters outside the view frustum, write a new cull visitor that does all the wacky things we'd need it to.
    _camera->setComputeNearFarMode(osg::Camera::DO_NOT_COMPUTE_NEAR_FAR);

    // switch off small feature culling as this can cull out geometry that will still be large enough once perspective correction takes effect.
    _camera->setCullingMode(_camera->getCullingMode() & ~osg::CullSettings::SMALL_FEATURE_CULLING);

    // set viewport
    _camera->setViewport(0,0,textureSize.x(),textureSize.y());


    if (debug)
    {
        // clear just the depth buffer
        _camera->setClearMask(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);

        // render after the main camera
        _camera->setRenderOrder(osg::Camera::POST_RENDER);

        // attach the texture and use it as the color buffer.
        //_camera->attach(osg::Camera::DEPTH_BUFFER, _texture.get());
        _camera->attach(osg::Camera::COLOR_BUFFER, _texture.get());
    }
    else
    {
        // clear the depth and colour bufferson each clear.
        _camera->setClearMask(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);

        // set the camera to render before the main camera.
        _camera->setRenderOrder(osg::Camera::PRE_RENDER);

        // tell the camera to use OpenGL frame buffer object where supported.
        _camera->setRenderTargetImplementation(osg::Camera::FRAME_BUFFER_OBJECT);

        // attach the texture and use it as the color buffer.
        _camera->attach(osg::Camera::DEPTH_BUFFER, _texture.get());
        //_camera->attach(osg::Camera::COLOR_BUFFER, _texture.get());
    }
}

void MWShadowTechnique::ShadowData::releaseGLObjects(osg::State* state) const
{
    OSG_INFO<<"MWShadowTechnique::ShadowData::releaseGLObjects"<<std::endl;
    _texture->releaseGLObjects(state);
    _camera->releaseGLObjects(state);
}

void MWShadowTechnique::ShadowData::updateTextureSize()
{
    const ShadowSettings* settings = _viewDependentData->getViewDependentShadowMap()->getShadowedScene()->getShadowSettings();

    const osg::Vec2s textureSize = settings->getDebugDraw() ? osg::Vec2s(512, 512) : settings->getTextureSize();
    if (_texture->getTextureWidth() == textureSize.x() || _texture->getTextureHeight() == textureSize.y())
        return;

    _texture->setTextureSize(textureSize.x(), textureSize.y());
    _texture->dirtyTextureObject();
    _camera->dirtyAttachmentMap();

    _camera->setViewport(0, 0, textureSize.x(), textureSize.y());
}

///////////////////////////////////////////////////////////////////////////////////////////////
//
// Frustum
//
MWShadowTechnique::Frustum::Frustum(osgUtil::CullVisitor* cv, double minZNear, double maxZFar):
    useCustomClipSpace(false),
    corners(8),
    faces(6),
    edges(12)
{
    projectionMatrix = *(cv->getProjectionMatrix());
    modelViewMatrix = *(cv->getModelViewMatrix());

    OSG_INFO<<"Projection matrix "<<projectionMatrix<<std::endl;

    if (cv->getComputeNearFarMode()!=osg::CullSettings::DO_NOT_COMPUTE_NEAR_FAR)
    {
        auto zNear = osg::maximum<osg::Matrix::value_type>(cv->getCalculatedNearPlane(),minZNear);
        auto zFar = osg::minimum<osg::Matrix::value_type>(cv->getCalculatedFarPlane(),maxZFar);
        if (zFar < 0)
            zFar = minZNear;
        zNear = std::min(zNear, zFar);

        cv->clampProjectionMatrix(projectionMatrix, zNear, zFar);

        OSG_INFO<<"zNear = "<<zNear<<", zFar = "<<zFar<<std::endl;
        OSG_INFO<<"Projection matrix after clamping "<<projectionMatrix<<std::endl;
    }
}

void SceneUtil::MWShadowTechnique::Frustum::setCustomClipSpace(const osg::BoundingBoxd& clipCornersOverride)
{
    useCustomClipSpace = true;
    customClipSpace = clipCornersOverride;
}

void SceneUtil::MWShadowTechnique::Frustum::init()
{
    osg::Matrixd clipToWorld;
    clipToWorld.invert(modelViewMatrix * projectionMatrix);

    if (useCustomClipSpace)
    {
        corners.clear();
        // Add corners in the same order OSG expects them
        for (int i : {0, 1, 5, 4, 2, 3, 7, 6})
        {
            corners.push_back(customClipSpace.corner(i));
        }
    }
    else
    {
        corners[0].set(-1.0, -1.0, -1.0);
        corners[1].set(1.0, -1.0, -1.0);
        corners[2].set(1.0, -1.0, 1.0);
        corners[3].set(-1.0, -1.0, 1.0);
        corners[4].set(-1.0, 1.0, -1.0);
        corners[5].set(1.0, 1.0, -1.0);
        corners[6].set(1.0, 1.0, 1.0);
        corners[7].set(-1.0, 1.0, 1.0);
    }

    // transform frustum corners from clipspace to world coords, and compute center
    for(auto & corner : corners)
    {
        corner = corner * clipToWorld;

        OSG_INFO<<"   corner "<<corner<<std::endl;
    }

    // compute eye point
    eye = osg::Vec3d(0.0,0.0,0.0) * osg::Matrix::inverse(modelViewMatrix);

    // compute center and the frustumCenterLine
    centerNearPlane = (corners[0]+corners[1]+corners[5]+corners[4])*0.25;
    centerFarPlane = (corners[3]+corners[2]+corners[6]+corners[7])*0.25;
    center = (centerNearPlane+centerFarPlane)*0.5;
    frustumCenterLine = centerFarPlane-centerNearPlane;
    frustumCenterLine.normalize();

    OSG_INFO<<"   center "<<center<<std::endl;

    faces[0].push_back(0);
    faces[0].push_back(3);
    faces[0].push_back(7);
    faces[0].push_back(4);

    faces[1].push_back(1);
    faces[1].push_back(5);
    faces[1].push_back(6);
    faces[1].push_back(2);

    faces[2].push_back(0);
    faces[2].push_back(1);
    faces[2].push_back(2);
    faces[2].push_back(3);

    faces[3].push_back(4);
    faces[3].push_back(7);
    faces[3].push_back(6);
    faces[3].push_back(5);

    faces[4].push_back(0);
    faces[4].push_back(4);
    faces[4].push_back(5);
    faces[4].push_back(1);

    faces[5].push_back(2);
    faces[5].push_back(6);
    faces[5].push_back(7);
    faces[5].push_back(3);

    edges[0].push_back(0); edges[0].push_back(1); // corner points on edge
    edges[0].push_back(2); edges[0].push_back(4); // faces on edge

    edges[1].push_back(1); edges[1].push_back(2); // corner points on edge
    edges[1].push_back(2); edges[1].push_back(1); // faces on edge

    edges[2].push_back(2); edges[2].push_back(3); // corner points on edge
    edges[2].push_back(2); edges[2].push_back(5); // faces on edge

    edges[3].push_back(3); edges[3].push_back(0); // corner points on edge
    edges[3].push_back(2); edges[3].push_back(0); // faces on edge


    edges[4].push_back(0); edges[4].push_back(4); // corner points on edge
    edges[4].push_back(0); edges[4].push_back(4); // faces on edge

    edges[5].push_back(1); edges[5].push_back(5); // corner points on edge
    edges[5].push_back(4); edges[5].push_back(1); // faces on edge

    edges[6].push_back(2); edges[6].push_back(6); // corner points on edge
    edges[6].push_back(1); edges[6].push_back(5); // faces on edge

    edges[7].push_back(3); edges[7].push_back(7); // corner points on edge
    edges[7].push_back(5); edges[7].push_back(0); // faces on edge


    edges[8].push_back(4); edges[8].push_back(5); // corner points on edge
    edges[8].push_back(3); edges[8].push_back(4); // faces on edge

    edges[9].push_back(5); edges[9].push_back(6); // corner points on edge
    edges[9].push_back(3); edges[9].push_back(1); // faces on edge

    edges[10].push_back(6);edges[10].push_back(7); // corner points on edge
    edges[10].push_back(3);edges[10].push_back(5); // faces on edge

    edges[11].push_back(7); edges[11].push_back(4); // corner points on edge
    edges[11].push_back(3); edges[11].push_back(0); // faces on edge
}


///////////////////////////////////////////////////////////////////////////////////////////////
//
// ViewDependentData
//
MWShadowTechnique::ViewDependentData::ViewDependentData(MWShadowTechnique* vdsm):
    _viewDependentShadowMap(vdsm)
{
    OSG_INFO<<"ViewDependentData::ViewDependentData()"<<this<<std::endl;
    for (auto& perFrameStateset : _stateset)
        perFrameStateset = new osg::StateSet;
}

void MWShadowTechnique::ViewDependentData::releaseGLObjects(osg::State* state) const
{
    for(const auto & itr : _shadowDataList)
    {
        itr->releaseGLObjects(state);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////
//
// MWShadowTechnique
//
MWShadowTechnique::MWShadowTechnique():
    ShadowTechnique(),
    _enableShadows(false),
    _debugHud(nullptr),
    _castingPrograms{ nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr }
{
    _shadowRecievingPlaceholderStateSet = new osg::StateSet;
    mSetDummyStateWhenDisabled = false;
}

MWShadowTechnique::MWShadowTechnique(const MWShadowTechnique& vdsm, const osg::CopyOp& copyop):
    ShadowTechnique(vdsm,copyop)
    , _castingPrograms(vdsm._castingPrograms)
{
    _shadowRecievingPlaceholderStateSet = new osg::StateSet;
    _enableShadows = vdsm._enableShadows;
    mSetDummyStateWhenDisabled = vdsm.mSetDummyStateWhenDisabled;
}

MWShadowTechnique::~MWShadowTechnique()
{
    if (_shadowsBin != nullptr)
        osgUtil::RenderBin::removeRenderBinPrototype(_shadowsBin);
}


void MWShadowTechnique::init()
{
    if (!_shadowedScene) return;

    OSG_INFO<<"MWShadowTechnique::init()"<<std::endl;

    createShaders();

    _dirty = false;
}

void MWShadowTechnique::cleanSceneGraph()
{
    OSG_INFO<<"MWShadowTechnique::cleanSceneGraph()"<<std::endl;
}

void MWShadowTechnique::enableShadows()
{
    _enableShadows = true;
}

void MWShadowTechnique::disableShadows(bool setDummyState)
{
    _enableShadows = false;
    mSetDummyStateWhenDisabled = setDummyState;
}

void SceneUtil::MWShadowTechnique::enableDebugHUD()
{
    _debugHud = new DebugHUD(static_cast<int>(getShadowedScene()->getShadowSettings()->getNumShadowMapsPerLight()));
}

void SceneUtil::MWShadowTechnique::disableDebugHUD()
{
    _debugHud = nullptr;
}

void SceneUtil::MWShadowTechnique::setSplitPointUniformLogarithmicRatio(double ratio)
{
    _splitPointUniformLogRatio = ratio;
}

void SceneUtil::MWShadowTechnique::setSplitPointDeltaBias(double bias)
{
    _splitPointDeltaBias = bias;
}

void SceneUtil::MWShadowTechnique::setPolygonOffset(float factor, float units)
{
    _polygonOffsetFactor = factor;
    _polygonOffsetUnits = units;

    if (_polygonOffset)
    {
        _polygonOffset->setFactor(factor);
        _polygonOffset->setUnits(units);
    }
}

void MWShadowTechnique::setMaximumShadowMapDistance(float distance)
{
    if (_shadowedScene && _shadowedScene->getShadowSettings())
        _shadowedScene->getShadowSettings()->setMaximumShadowMapDistance(distance);

    osg::ref_ptr<osg::Uniform> newDistance = new osg::Uniform("maximumShadowMapDistance", distance);
    for (auto& list : _uniforms)
    {
        for (auto& u : list)
        {
            if (u->getName() == "maximumShadowMapDistance")
                u = newDistance;
        }
    }
}

void SceneUtil::MWShadowTechnique::setShadowFadeStart(float shadowFadeStart)
{
    _shadowFadeStart = shadowFadeStart;

    osg::ref_ptr<osg::Uniform> newFadeStart = new osg::Uniform("shadowFadeStart", shadowFadeStart);
    for (auto& list : _uniforms)
    {
        for (auto& u : list)
        {
            if (u->getName() == "shadowFadeStart")
                u = newFadeStart;
        }
    }
}

void SceneUtil::MWShadowTechnique::enableFrontFaceCulling()
{
    _useFrontFaceCulling = true;

    if (_shadowCastingStateSet)
    {
        _shadowCastingStateSet->setAttribute(new osg::CullFace(osg::CullFace::FRONT), osg::StateAttribute::ON | osg::StateAttribute::OVERRIDE);
        _shadowCastingStateSet->setMode(GL_CULL_FACE, osg::StateAttribute::OFF);
    }
}

void SceneUtil::MWShadowTechnique::disableFrontFaceCulling()
{
    _useFrontFaceCulling = false;

    if (_shadowCastingStateSet)
    {
        _shadowCastingStateSet->removeAttribute(osg::StateAttribute::CULLFACE);
        _shadowCastingStateSet->setMode(GL_CULL_FACE, osg::StateAttribute::OFF | osg::StateAttribute::OVERRIDE);
    }
}

void SceneUtil::MWShadowTechnique::setupCastingShader(Shader::ShaderManager & shaderManager)
{
    // This can't be part of the constructor as OSG mandates that there be a trivial constructor available

    osg::ref_ptr<osg::Shader> castingVertexShader = shaderManager.getShader("shadowcasting.vert");
    std::string useGPUShader4 = SceneUtil::getGLExtensions().isGpuShader4Supported ? "1" : "0";
    for (int alphaFunc = GL_NEVER; alphaFunc <= GL_ALWAYS; ++alphaFunc)
    {
        auto& program = _castingPrograms[alphaFunc - GL_NEVER];
        program = shaderManager.getProgram(castingVertexShader,
            shaderManager.getShader("shadowcasting.frag",
                { { "alphaFunc", std::to_string(alphaFunc) }, { "alphaToCoverage", "0" }, { "adjustCoverage", "1" },
                    { "useGPUShader4", useGPUShader4 } }));
    }
}

MWShadowTechnique::ViewDependentData* MWShadowTechnique::createViewDependentData(osgUtil::CullVisitor* /*cv*/)
{
    return new ViewDependentData(this);
}

MWShadowTechnique::ViewDependentData* MWShadowTechnique::getViewDependentData(osgUtil::CullVisitor* cv)
{
    std::scoped_lock lock(_viewDependentDataMapMutex);
    auto itr = _viewDependentDataMap.find(cv);
    if (itr!=_viewDependentDataMap.end()) return itr->second.get();

    osg::ref_ptr<ViewDependentData> vdd = createViewDependentData(cv);
    _viewDependentDataMap[cv] = vdd;
    return vdd.release();
}

void SceneUtil::MWShadowTechnique::copyShadowMap(osgUtil::CullVisitor& cv, ViewDependentData* lhs, ViewDependentData* rhs)
{
    // Prepare for rendering shadows using the shadow map owned by rhs.

    // To achieve this i first copy all data that is not specific to this cv's camera and thus read-only,
    // trusting openmw and osg won't overwrite that data before this frame is done rendering.
    // This works due to the double buffering of CullVisitors by osg, but also requires that cull passes are serialized (relative to one another).
    // Then initialize new copies of the data that will be written with view-specific data 
    // (the stateset and the texgens).

    lhs->_viewDependentShadowMap = rhs->_viewDependentShadowMap;
    auto* stateset = lhs->getStateSet(cv.getTraversalNumber());
    stateset->clear();
    lhs->_lightDataList = rhs->_lightDataList;
    lhs->_numValidShadows = rhs->_numValidShadows;

    ShadowDataList& sdl = lhs->getShadowDataList();
    ShadowDataList previous_sdl;
    previous_sdl.swap(sdl);
    for (const auto& rhs_sd : rhs->getShadowDataList())
    {
        osg::ref_ptr<ShadowData> lhs_sd;

        if (previous_sdl.empty())
        {
            OSG_INFO << "Create new ShadowData" << std::endl;
            lhs_sd = new ShadowData(lhs);
        }
        else
        {
            OSG_INFO << "Taking ShadowData from from of previous_sdl" << std::endl;
            lhs_sd = previous_sdl.front();
            previous_sdl.erase(previous_sdl.begin());
        }
        lhs_sd->_camera = rhs_sd->_camera;
        lhs_sd->_textureUnit = rhs_sd->_textureUnit;
        lhs_sd->_texture = rhs_sd->_texture;
        lhs_sd->_sm_i = rhs_sd->_sm_i;
        sdl.push_back(lhs_sd);
    }

    copyShadowStateSettings(cv, lhs);

    if (lhs->_numValidShadows > 0)
    {
        prepareStateSetForRenderingShadow(*lhs, cv.getTraversalNumber());
    }
}

void SceneUtil::MWShadowTechnique::setCustomFrustumCallback(CustomFrustumCallback* cfc)
{
    _customFrustumCallback = cfc;
}

void SceneUtil::MWShadowTechnique::copyShadowStateSettings(osgUtil::CullVisitor& cv, ViewDependentData* vdd)
{
    for (const auto& sd : vdd->getShadowDataList())
    {
        assignValidRegionSettings(cv, sd->_camera, sd->_sm_i, vdd->_uniforms[cv.getTraversalNumber()%2]);
        assignShadowStateSettings(cv, sd->_camera, sd->_sm_i, vdd->_uniforms[cv.getTraversalNumber()%2]);
    }
}

void MWShadowTechnique::update(osg::NodeVisitor& nv)
{
    OSG_INFO<<"MWShadowTechnique::update(osg::NodeVisitor& "<<&nv<<")"<<std::endl;
    _shadowedScene->osg::Group::traverse(nv);
}

void MWShadowTechnique::cull(osgUtil::CullVisitor& cv)
{

    if (!_enableShadows)
    {
        if (mSetDummyStateWhenDisabled)
        {
            osg::ref_ptr<osg::StateSet> dummyState = new osg::StateSet();

            ShadowSettings* settings = getShadowedScene()->getShadowSettings();
            int baseUnit = static_cast<int>(settings->getBaseShadowTextureUnit());
            int endUnit = static_cast<int>(baseUnit + settings->getNumShadowMapsPerLight());
            for (int i = baseUnit; i < endUnit; ++i)
            {
                dummyState->setTextureAttribute(i, _fallbackShadowMapTexture, osg::StateAttribute::ON);
                dummyState->addUniform(new osg::Uniform(("shadowTexture" + std::to_string(i - baseUnit)).c_str(), i));
            }

            cv.pushStateSet(dummyState);
        }

        _shadowedScene->osg::Group::traverse(cv);

        if (mSetDummyStateWhenDisabled)
            cv.popStateSet();

        return;
    }

    OSG_INFO<<std::endl<<std::endl<<"MWShadowTechnique::cull(osg::CullVisitor&"<<&cv<<")"<<std::endl;

    if (!_shadowCastingStateSet)
    {
        OSG_INFO<<"Warning, init() has not yet been called so ShadowCastingStateSet has not been setup yet, unable to create shadows."<<std::endl;
        _shadowedScene->osg::Group::traverse(cv);
        return;
    }

    ViewDependentData* vdd = getViewDependentData(&cv);

    if (!vdd)
    {
        OSG_INFO<<"Warning, now ViewDependentData created, unable to create shadows."<<std::endl;
        _shadowedScene->osg::Group::traverse(cv);
        return;
    }
    
    Uniforms& vddUniforms = vdd->_uniforms[cv.getTraversalNumber() % 2];

    ShadowSettings* settings = getShadowedScene()->getShadowSettings();

    OSG_INFO<<"cv->getProjectionMatrix()="<<*cv.getProjectionMatrix()<<std::endl;

    osg::CullSettings::ComputeNearFarMode cachedNearFarMode = cv.getComputeNearFarMode();

    osg::RefMatrix& viewProjectionMatrix = *cv.getProjectionMatrix();

    // check whether this main views projection is perspective or orthographic
    bool orthographicViewFrustum = viewProjectionMatrix(0,3)==0 &&
                                   viewProjectionMatrix(1,3)==0 &&
                                   viewProjectionMatrix(2,3)==0;

    double minZNear = 0.0;
    double maxZFar = sDoubleMost;

    if (cachedNearFarMode==osg::CullSettings::DO_NOT_COMPUTE_NEAR_FAR)
    {
        double left = NAN, right = NAN, top = NAN, bottom = NAN;
        if (orthographicViewFrustum)
        {
            viewProjectionMatrix.getOrtho(left, right, bottom, top, minZNear, maxZFar);
        }
        else
        {
            viewProjectionMatrix.getFrustum(left, right, bottom, top, minZNear, maxZFar);
        }
        OSG_INFO<<"minZNear="<<minZNear<<", maxZFar="<<maxZFar<<std::endl;
    }

    // set the compute near/far mode to the highest quality setting to ensure we push the near plan out as far as possible
    if (settings->getComputeNearFarModeOverride()!=osg::CullSettings::DO_NOT_COMPUTE_NEAR_FAR)
    {
        cv.setComputeNearFarMode(settings->getComputeNearFarModeOverride());
    }

    // 1. Traverse main scene graph
    auto* shadowReceiverStateSet = vdd->getStateSet(cv.getTraversalNumber());
    shadowReceiverStateSet->clear();
    cv.pushStateSet(shadowReceiverStateSet);

    cullShadowReceivingScene(&cv);

    cv.popStateSet();

    if (cv.getComputeNearFarMode()!=osg::CullSettings::DO_NOT_COMPUTE_NEAR_FAR)
    {
        OSG_INFO<<"Just done main subgraph traversak"<<std::endl;
        // make sure that the near plane is computed correctly so that any projection matrix computations
        // are all done correctly.
        cv.computeNearPlane();
    }
 
    // clamp the minZNear and maxZFar to those provided by ShadowSettings
    maxZFar = osg::minimum(settings->getMaximumShadowMapDistance(),maxZFar);
    if (minZNear>maxZFar) minZNear = maxZFar*settings->getMinimumShadowMapNearFarRatio();

    //OSG_NOTICE<<"maxZFar "<<maxZFar<<std::endl;

    // Workaround for absurdly huge viewing distances where OSG would otherwise push the near plane out.
    cv.setNearFarRatio(minZNear / maxZFar);

    Frustum frustum(&cv, minZNear, maxZFar);
    if (_customFrustumCallback)
    {
        OSG_INFO << "Calling custom frustum callback" << std::endl;
        osgUtil::CullVisitor* sharedFrustumHint = nullptr;
        _customClipSpace.init();
        _customFrustumCallback->operator()(cv, _customClipSpace, sharedFrustumHint);
        frustum.setCustomClipSpace(_customClipSpace);
        if (sharedFrustumHint)
        {
            // user hinted another view shares its frustum
            std::scoped_lock lock(_viewDependentDataMapMutex);
            auto itr = _viewDependentDataMap.find(sharedFrustumHint);
            if (itr != _viewDependentDataMap.end())
            {
                OSG_INFO << "User provided a valid shared frustum hint, re-using previously generated shadow map" << std::endl;

                copyShadowMap(cv, vdd, itr->second);

                // return compute near far mode back to it's original settings
                cv.setComputeNearFarMode(cachedNearFarMode);
                return;
            }
            else
            {
                OSG_INFO << "User provided a shared frustum hint, but it was not valid." << std::endl;
            }
        }
    }

    frustum.init();
    if (_debugHud)
    {
        osg::ref_ptr<osg::Vec3Array> vertexArray = new osg::Vec3Array();
        for (osg::Vec3d &vertex : frustum.corners)
            vertexArray->push_back(osg::Vec3(vertex));
        _debugHud->setFrustumVertices(vertexArray, cv.getTraversalNumber());
    }

    double reducedNear = NAN, reducedFar = NAN;
    if (cv.getComputeNearFarMode() != osg::CullSettings::DO_NOT_COMPUTE_NEAR_FAR)
    {
        reducedNear = osg::maximum<double>(cv.getCalculatedNearPlane(), minZNear);
        reducedFar = osg::minimum<double>(cv.getCalculatedFarPlane(), maxZFar);
    }
    else
    {
        reducedNear = minZNear;
        reducedFar = maxZFar;
    }
 
    // return compute near far mode back to it's original settings
    cv.setComputeNearFarMode(cachedNearFarMode);

    OSG_INFO<<"frustum.eye="<<frustum.eye<<", frustum.centerNearPlane, "<<frustum.centerNearPlane<<" distance = "<<(frustum.eye-frustum.centerNearPlane).length()<<std::endl;


    // 2. select active light sources
    //    create a list of light sources + their matrices to place them
    selectActiveLights(&cv, vdd);


    unsigned int pos_x = 0;
    unsigned int textureUnit = settings->getBaseShadowTextureUnit();
    unsigned int numValidShadows = 0;

    ShadowDataList& sdl = vdd->getShadowDataList();
    ShadowDataList previous_sdl;
    previous_sdl.swap(sdl);

    unsigned int numShadowMapsPerLight = settings->getNumShadowMapsPerLight();

    LightDataList& pll = vdd->getLightDataList();
    for(auto & itr : pll)
    {
        // 3. create per light/per shadow map division of lightspace/frustum
        //    create a list of light/shadow map data structures

        LightData& pl = *itr;

        // 3.1 compute light space polytope
        //
        osg::Polytope polytope = computeLightViewFrustumPolytope(frustum, pl);

        // if polytope is empty then no rendering.
        if (polytope.empty())
        {
            OSG_NOTICE<<"Polytope empty no shadow to render"<<std::endl;
            continue;
        }

        // 3.2 compute RTT camera view+projection matrix settings
        //
        osg::Matrixd projectionMatrix;
        osg::Matrixd viewMatrix;
        if (!computeShadowCameraSettings(frustum, pl, projectionMatrix, viewMatrix))
        {
            OSG_NOTICE<<"No valid Camera settings, no shadow to render"<<std::endl;
            continue;
        }

        // if we are using multiple shadow maps and CastShadowTraversalMask is being used
        // traverse the scene to compute the extents of the objects
        if (/*numShadowMapsPerLight>1 &&*/ (_shadowedScene->getCastsShadowTraversalMask() & _worldMask) == 0)
        {
            // osg::ElapsedTime timer;

            osg::ref_ptr<osg::Viewport> viewport = new osg::Viewport(0,0,2048,2048);
            if (!_clsb) _clsb = new ComputeLightSpaceBounds;
            ComputeLightSpaceBounds& clsb = *_clsb;
            clsb.reset();
            clsb.pushViewport(viewport);
            clsb.pushProjectionMatrix(new osg::RefMatrix(projectionMatrix));
            clsb.pushModelViewMatrix(new osg::RefMatrix(viewMatrix), osg::Transform::ABSOLUTE_RF);
            clsb.setTraversalMask(_shadowedScene->getCastsShadowTraversalMask());

            osg::Matrixd invertModelView;
            invertModelView.invert(viewMatrix);
            osg::Polytope local_polytope(polytope);
            local_polytope.transformProvidingInverse(invertModelView);

            osg::CullingSet& cs = clsb.getProjectionCullingStack().back();
            cs.setFrustum(local_polytope);
            clsb.pushCullingSet();

            _shadowedScene->accept(clsb);

            clsb.popCullingSet();

            clsb.popModelViewMatrix();
            clsb.popProjectionMatrix();
            clsb.popViewport();

            // OSG_NOTICE<<"Extents of LightSpace "<<clsb._bb.xMin()<<", "<<clsb._bb.xMax()<<", "<<clsb._bb.yMin()<<", "<<clsb._bb.yMax()<<", "<<clsb._bb.zMin()<<", "<<clsb._bb.zMax()<<std::endl;
            // OSG_NOTICE<<"  time "<<timer.elapsedTime_m()<<"ms, mask = "<<std::hex<<_shadowedScene->getCastsShadowTraversalMask()<<std::endl;

            if (clsb._bb.xMin()>-1.0f || clsb._bb.xMax()<1.0f || clsb._bb.yMin()>-1.0f || clsb._bb.yMax()<1.0f)
            {
                // OSG_NOTICE<<"Need to clamp projection matrix"<<std::endl;

                double xMid = (clsb._bb.xMin()+clsb._bb.xMax())*0.5f;
                double xRange = clsb._bb.xMax()-clsb._bb.xMin();
                double yMid = (clsb._bb.yMin()+clsb._bb.yMax())*0.5f;
                double yRange = (clsb._bb.yMax()-clsb._bb.yMin());

                osg::Matrixd cornerConverter = osg::Matrixd::inverse(projectionMatrix) * osg::Matrixd::inverse(viewMatrix) * *cv.getModelViewMatrix();
                double minZ = sDoubleMost;
                double maxZ = -sDoubleMost;
                clsb._bb._max[2] = 1.0;
                for (unsigned int i = 0; i < 8; i++)
                {
                    osg::Vec3 corner = clsb._bb.corner(i);
                    corner = corner * cornerConverter;

                    maxZ = osg::maximum<double>(maxZ, -corner.z());
                    minZ = osg::minimum<double>(minZ, -corner.z());
                }
                reducedNear = osg::maximum<double>(reducedNear, minZ);
                reducedFar = osg::minimum<double>(reducedFar, maxZ);

                // OSG_NOTICE<<"  xMid="<<xMid<<", yMid="<<yMid<<", xRange="<<xRange<<", yRange="<<yRange<<std::endl;

                projectionMatrix =
                    projectionMatrix *
                    osg::Matrixd::translate(osg::Vec3d(-xMid,-yMid,0.0)) *
                    osg::Matrixd::scale(osg::Vec3d(2.0/xRange, 2.0/yRange,1.0));

            }

        }


        const bool debug = settings->getDebugDraw();

        // 4. For each light/shadow map
        for (unsigned int sm_i=0; sm_i<numShadowMapsPerLight; ++sm_i)
        {
            osg::ref_ptr<ShadowData> sd;

            if (previous_sdl.empty())
            {
                OSG_INFO<<"Create new ShadowData"<<std::endl;
                sd = new ShadowData(vdd);
            }
            else
            {
                OSG_INFO<<"Taking ShadowData from from of previous_sdl"<<std::endl;
                sd = previous_sdl.front();
                previous_sdl.erase(previous_sdl.begin());

                if (!debug)
                    sd->updateTextureSize();
            }

            osg::ref_ptr<osg::Camera> camera = sd->_camera;

            camera->setProjectionMatrix(projectionMatrix);
            camera->setViewMatrix(viewMatrix);

            if (debug)
            {
                camera->getViewport()->x() = pos_x;
                pos_x += static_cast<unsigned int>(camera->getViewport()->width()) + 40;
            }

            if (_useFrontFaceCulling)
                camera->setCullingMode(camera->getCullingMode() | osg::CullSettings::CLUSTER_CULLING);
            else
                camera->setCullingMode(camera->getCullingMode() & ~osg::CullSettings::CLUSTER_CULLING);

            // transform polytope in model coords into light spaces eye coords.
            osg::Matrixd invertModelView;
            invertModelView.invert(camera->getViewMatrix());

            osg::Polytope local_polytope(polytope);
            local_polytope.transformProvidingInverse(invertModelView);

            double cascaseNear = reducedNear;
            double cascadeFar = reducedFar;
            if (numShadowMapsPerLight>1)
            {
                // compute the start and end range in non-dimensional coords
                double r_start = NAN, r_end = NAN;

                // split system based on the original Parallel Split Shadow Maps paper.
                double n = reducedNear;
                double f = reducedFar;
                auto i = static_cast<double>(sm_i);
                auto m = static_cast<double>(numShadowMapsPerLight);
                if (sm_i == 0)
                    r_start = -1.0;
                else
                {
                    // compute the split point in main camera view
                    double ciLog = n * pow(f / n, i / m);
                    double ciUniform = n + (f - n) * i / m;
                    double ci = _splitPointUniformLogRatio * ciLog + (1.0 - _splitPointUniformLogRatio) * ciUniform + _splitPointDeltaBias;
                    cascaseNear = ci;

                    // work out where this is in light space
                    osg::Vec3d worldSpacePos = frustum.eye + frustum.frustumCenterLine * ci;
                    osg::Vec3d lightSpacePos = worldSpacePos * viewMatrix * projectionMatrix;
                    r_start = lightSpacePos.y();
                }

                if (sm_i + 1 == numShadowMapsPerLight)
                    r_end = 1.0;
                else
                {
                    // compute the split point in main camera view
                    double ciLog = n * pow(f / n, (i + 1) / m);
                    double ciUniform = n + (f - n) * (i + 1) / m;
                    double ci = _splitPointUniformLogRatio * ciLog + (1.0 - _splitPointUniformLogRatio) * ciUniform + _splitPointDeltaBias;
                    cascadeFar = ci;
                    
                    // work out where this is in light space
                    osg::Vec3d worldSpacePos = frustum.eye + frustum.frustumCenterLine * ci;
                    osg::Vec3d lightSpacePos = worldSpacePos * viewMatrix * projectionMatrix;
                    r_end = lightSpacePos.y();
                }
                // for all by the last shadowmap shift the r_end so that it overlaps slightly with the next shadowmap
                // to prevent a seam showing through between the shadowmaps
                if (sm_i+1<numShadowMapsPerLight) r_end+=0.01;


                // We can't add these clipping planes with cascaded shadow maps as they wouldn't be parallel to the light direction.

                if (settings->getMultipleShadowMapHint() == ShadowSettings::PARALLEL_SPLIT && sm_i>0)
                {
                    // not the first shadowmap so insert a polytope to clip the scene from before r_start

                    // plane in clip space coords
                    osg::Plane plane(0.0,1.0,0.0,-r_start);

                    // transform into eye coords
                    plane.transformProvidingInverse(projectionMatrix);
                    local_polytope.getPlaneList().push_back(plane);

                    //OSG_NOTICE<<"Adding r_start plane "<<plane<<std::endl;

                }

                if (settings->getMultipleShadowMapHint() == ShadowSettings::PARALLEL_SPLIT && sm_i+1<numShadowMapsPerLight)
                {
                    // not the last shadowmap so insert a polytope to clip the scene from beyond r_end

                    // plane in clip space coords
                    osg::Plane plane(0.0,-1.0,0.0,r_end);

                    // transform into eye coords
                    plane.transformProvidingInverse(projectionMatrix);
                    local_polytope.getPlaneList().push_back(plane);

                    //OSG_NOTICE<<"Adding r_end plane "<<plane<<std::endl;
                }

                local_polytope.setupMask();

                if (settings->getMultipleShadowMapHint() == ShadowSettings::PARALLEL_SPLIT)
                {
                    // OSG_NOTICE<<"Need to adjust RTT camera projection and view matrix here, r_start="<<r_start<<", r_end="<<r_end<<std::endl;
                    // OSG_NOTICE<<"  textureUnit = "<<textureUnit<<std::endl;

                    double mid_r = (r_start + r_end)*0.5;
                    double range_r = (r_end - r_start);

                    // OSG_NOTICE<<"  mid_r = "<<mid_r<<", range_r = "<<range_r<<std::endl;

                    camera->setProjectionMatrix(
                        camera->getProjectionMatrix() *
                        osg::Matrixd::translate(osg::Vec3d(0.0,-mid_r,0.0)) *
                        osg::Matrixd::scale(osg::Vec3d(1.0,2.0/range_r,1.0)));
                    
                }
            }

            std::vector<osg::Plane> extraPlanes;
            if (settings->getMultipleShadowMapHint() == ShadowSettings::CASCADED)
            {
                cropShadowCameraToMainFrustum(frustum, camera, cascaseNear, cascadeFar, extraPlanes);
                for (const auto& plane : extraPlanes)
                    local_polytope.getPlaneList().push_back(plane);
                local_polytope.setupMask();
            }
            else
                cropShadowCameraToMainFrustum(frustum, camera, reducedNear, reducedFar, extraPlanes);

            osg::ref_ptr<VDSMCameraCullCallback> vdsmCallback = new VDSMCameraCullCallback(this, local_polytope);
            camera->setCullCallback(vdsmCallback.get());

            // 4.3 traverse RTT camera
            //

            cv.pushStateSet(_shadowCastingStateSet.get());

            cullShadowCastingScene(&cv, camera.get());

            cv.popStateSet();

            if (!orthographicViewFrustum && settings->getShadowMapProjectionHint()==ShadowSettings::PERSPECTIVE_SHADOW_MAP)
            {
                assignValidRegionSettings(cv, camera, sm_i, vddUniforms);

                if (settings->getMultipleShadowMapHint() == ShadowSettings::CASCADED)
                    adjustPerspectiveShadowMapCameraSettings(vdsmCallback->getRenderStage(), frustum, pl, camera.get(), cascaseNear, cascadeFar);
                else
                    adjustPerspectiveShadowMapCameraSettings(vdsmCallback->getRenderStage(), frustum, pl, camera.get(), reducedNear, reducedFar);
                if (vdsmCallback->getProjectionMatrix())
                {
                    vdsmCallback->getProjectionMatrix()->set(camera->getProjectionMatrix());
                }
            }
 
            // 4.4 compute main scene graph TexGen + uniform settings + setup state
            //
            {
                assignShadowStateSettings(cv, camera, sm_i, vddUniforms);
            }

            // mark the light as one that has active shadows and requires shaders
            pl.textureUnits.push_back(textureUnit);

            // pass on shadow data to ShadowDataList
            sd->_textureUnit = textureUnit;
            sd->_sm_i = sm_i;

            sdl.push_back(sd);

            // increment counters.
            ++textureUnit;
            ++numValidShadows ;

            if (_debugHud)
                _debugHud->draw(sd->_texture, sm_i, camera->getViewMatrix() * camera->getProjectionMatrix(), cv);
        }
    }

    vdd->setNumValidShadows(numValidShadows);

    if (numValidShadows>0)
    {
        prepareStateSetForRenderingShadow(*vdd, cv.getTraversalNumber());
    }

    // OSG_NOTICE<<"End of shadow setup Projection matrix "<<*cv.getProjectionMatrix()<<std::endl;
}

bool MWShadowTechnique::selectActiveLights(osgUtil::CullVisitor* cv, ViewDependentData* vdd) const
{
    OSG_INFO<<"selectActiveLights"<<std::endl;

    LightDataList& pll = vdd->getLightDataList();

    LightDataList previous_ldl;
    previous_ldl.swap(pll);

    //MR testing giving a specific light
    osgUtil::RenderStage * rs = cv->getCurrentRenderBin()->getStage();

    OSG_INFO<<"selectActiveLights osgUtil::RenderStage="<<rs<<std::endl;

    osg::Matrixd modelViewMatrix = *(cv->getModelViewMatrix());

    struct FindFirstLightManager : public osg::NodeVisitor
    {
        FindFirstLightManager()
            : osg::NodeVisitor(TRAVERSE_ALL_CHILDREN)
        {
        }

        void apply(osg::Group& node) override
        {
            if (auto* lm = dynamic_cast<LightManager*>(&node))
            {
                mLightManager = lm;
                mLocalToWorld = new osg::RefMatrix(osg::computeLocalToWorld(getNodePath()));
                return;
            }
            traverse(node);
        }
        LightManager* mLightManager = nullptr;
        osg::ref_ptr<osg::RefMatrix> mLocalToWorld;
    };

    FindFirstLightManager lightManagerFinder;

    _shadowedScene->accept(lightManagerFinder);

    if (lightManagerFinder.mLightManager && lightManagerFinder.mLightManager->getSunlight()) {
        auto* ld = new LightData(vdd);
        ld->setLightData(lightManagerFinder.mLocalToWorld, lightManagerFinder.mLightManager->getSunlight(), modelViewMatrix);
        pll.push_back(ld);
    }

    return !pll.empty();
}

void MWShadowTechnique::createShaders()
{
    OSG_INFO<<"MWShadowTechnique::createShaders()"<<std::endl;

    unsigned int _baseTextureUnit = 0;

    _shadowCastingStateSet = new osg::StateSet;

    ShadowSettings* settings = getShadowedScene()->getShadowSettings();

    if (!settings->getDebugDraw())
    {
        // note soft (attribute only no mode override) setting. When this works ?
        // 1. for objects prepared for backface culling
        //    because they usually also set CullFace and CullMode on in their state
        //    For them we override CullFace but CullMode remains set by them
        // 2. For one faced, trees, and similar objects which cannot use
        //    backface nor front face so they usually use CullMode off set here.
        //    In this case we will draw them in their entirety.

        if (_useFrontFaceCulling)
        {
            _shadowCastingStateSet->setAttribute(new osg::CullFace(osg::CullFace::FRONT), osg::StateAttribute::ON | osg::StateAttribute::OVERRIDE);

            // make sure GL_CULL_FACE is off by default
            // we assume that if object has cull face attribute set to back
            // it will also set cull face mode ON so no need for override
            _shadowCastingStateSet->setMode(GL_CULL_FACE, osg::StateAttribute::OFF);
        }
        else
            _shadowCastingStateSet->setMode(GL_CULL_FACE, osg::StateAttribute::OFF | osg::StateAttribute::OVERRIDE);
    }

    _polygonOffset = new osg::PolygonOffset(_polygonOffsetFactor, _polygonOffsetUnits);
    _shadowCastingStateSet->setAttribute(_polygonOffset.get(), osg::StateAttribute::ON | osg::StateAttribute::OVERRIDE);
    _shadowCastingStateSet->setMode(GL_POLYGON_OFFSET_FILL, osg::StateAttribute::ON | osg::StateAttribute::OVERRIDE);


    osg::ref_ptr<osg::Uniform> baseTextureSampler = new osg::Uniform("baseTexture",static_cast<int>(_baseTextureUnit));
    osg::ref_ptr<osg::Uniform> baseTextureUnit = new osg::Uniform("baseTextureUnit",static_cast<int>(_baseTextureUnit));

    osg::ref_ptr<osg::Uniform> maxDistance = new osg::Uniform("maximumShadowMapDistance", static_cast<float>(settings->getMaximumShadowMapDistance()));
    osg::ref_ptr<osg::Uniform> fadeStart = new osg::Uniform("shadowFadeStart", _shadowFadeStart);

    for (auto& perFrameUniformList : _uniforms)
    {
        perFrameUniformList.clear();
        perFrameUniformList.push_back(baseTextureSampler);
        perFrameUniformList.emplace_back(baseTextureUnit.get());
        perFrameUniformList.push_back(maxDistance);
        perFrameUniformList.push_back(fadeStart);
    }

    for(unsigned int sm_i=0; sm_i<settings->getNumShadowMapsPerLight(); ++sm_i)
    {
        {
            std::stringstream sstr;
            sstr<<"shadowTexture"<<sm_i;
            osg::ref_ptr<osg::Uniform> shadowTextureSampler = new osg::Uniform(sstr.str().c_str(),static_cast<int>(settings->getBaseShadowTextureUnit()+sm_i));
            for (auto& perFrameUniformList : _uniforms)
                perFrameUniformList.emplace_back(shadowTextureSampler.get());
        }
    }

    switch(settings->getShaderHint())
    {
        case(ShadowSettings::NO_SHADERS):
        {
            OSG_INFO<<"No shaders provided by, user must supply own shaders"<<std::endl;
            break;
        }
        case(ShadowSettings::PROVIDE_VERTEX_AND_FRAGMENT_SHADER):
        case(ShadowSettings::PROVIDE_FRAGMENT_SHADER):
        {
            _program = new osg::Program;

            //osg::ref_ptr<osg::Shader> fragment_shader = new osg::Shader(osg::Shader::FRAGMENT, fragmentShaderSource_noBaseTexture);
            if (settings->getNumShadowMapsPerLight()==2)
            {
                _program->addShader(new osg::Shader(osg::Shader::FRAGMENT, fragmentShaderSource_withBaseTexture_twoShadowMaps));
            }
            else
            {
                _program->addShader(new osg::Shader(osg::Shader::FRAGMENT, fragmentShaderSource_withBaseTexture));
            }

            break;
        }
    }

    {
        osg::ref_ptr<osg::Image> image = new osg::Image;
        image->allocateImage( 1, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE );
        *reinterpret_cast<osg::Vec4ub*>(image->data()) = osg::Vec4ub( 0xFF, 0xFF, 0xFF, 0xFF );

        _fallbackBaseTexture = new osg::Texture2D(image.get());
        _fallbackBaseTexture->setWrap(osg::Texture2D::WRAP_S,osg::Texture2D::REPEAT);
        _fallbackBaseTexture->setWrap(osg::Texture2D::WRAP_T,osg::Texture2D::REPEAT);
        _fallbackBaseTexture->setFilter(osg::Texture2D::MIN_FILTER,osg::Texture2D::NEAREST);
        _fallbackBaseTexture->setFilter(osg::Texture2D::MAG_FILTER,osg::Texture2D::NEAREST);

        _fallbackShadowMapTexture = new osg::Texture2D(image.get());
        _fallbackShadowMapTexture->setWrap(osg::Texture2D::WRAP_S,osg::Texture2D::REPEAT);
        _fallbackShadowMapTexture->setWrap(osg::Texture2D::WRAP_T,osg::Texture2D::REPEAT);
        _fallbackShadowMapTexture->setFilter(osg::Texture2D::MIN_FILTER,osg::Texture2D::NEAREST);
        _fallbackShadowMapTexture->setFilter(osg::Texture2D::MAG_FILTER,osg::Texture2D::NEAREST);
        _fallbackShadowMapTexture->setShadowComparison(true);
        _fallbackShadowMapTexture->setShadowCompareFunc(osg::Texture::ShadowCompareFunc::ALWAYS);

    }

    if (!_castingPrograms[GL_ALWAYS - GL_NEVER])
        OSG_NOTICE << "Shadow casting shader has not been set up. Remember to call setupCastingShader(Shader::ShaderManager &)" << std::endl;

    // Always use the GL_ALWAYS shader as the shadows bin will change it if necessary
    _shadowCastingStateSet->setAttributeAndModes(_castingPrograms[GL_ALWAYS - GL_NEVER], osg::StateAttribute::ON | osg::StateAttribute::OVERRIDE);
    // The casting program uses a sampler, so to avoid undefined behaviour, we must bind a dummy texture in case no other is supplied
    _shadowCastingStateSet->setTextureAttribute(0, _fallbackBaseTexture.get(), osg::StateAttribute::ON);
    _shadowCastingStateSet->addUniform(new osg::Uniform("useDiffuseMapForShadowAlpha", true));
    _shadowCastingStateSet->addUniform(new osg::Uniform("alphaTestShadows", false));
    osg::ref_ptr<osg::Depth> depth = new osg::Depth;
    depth->setWriteMask(true);
    osg::ref_ptr<osg::ClipControl> clipcontrol = new osg::ClipControl(osg::ClipControl::LOWER_LEFT, osg::ClipControl::NEGATIVE_ONE_TO_ONE);
    _shadowCastingStateSet->setAttribute(clipcontrol, osg::StateAttribute::ON|osg::StateAttribute::OVERRIDE);
    _shadowCastingStateSet->setAttribute(depth, osg::StateAttribute::ON|osg::StateAttribute::OVERRIDE);
    _shadowCastingStateSet->setMode(GL_DEPTH_CLAMP, osg::StateAttribute::ON);

    // TODO: compare performance when alpha testing is handled here versus using a discard in the fragment shader
}

osg::Polytope MWShadowTechnique::computeLightViewFrustumPolytope(Frustum& frustum, LightData& positionedLight)
{
    OSG_INFO<<"computeLightViewFrustumPolytope()"<<std::endl;

    osg::Polytope polytope;
    if (frustum.useCustomClipSpace)
    {
        polytope.setToBoundingBox(frustum.customClipSpace);
    }
    else
    {
        polytope.setToUnitFrustum();
    }

    polytope.transformProvidingInverse( frustum.projectionMatrix );
    polytope.transformProvidingInverse( frustum.modelViewMatrix );

    osg::Polytope lightVolumePolytope;

    if (positionedLight.directionalLight)
    {
        osg::Polytope::PlaneList& planes = polytope.getPlaneList();
        osg::Polytope::ClippingMask selector_mask = 0x1;
        osg::Polytope::ClippingMask result_mask = 0x0;
        for(unsigned int i=0; i<planes.size(); ++i, selector_mask <<= 1)
        {
            OSG_INFO<<"      plane "<<planes[i]<<"  planes["<<i<<"].dotProductNormal(lightDir)="<<planes[i].dotProductNormal(positionedLight.lightDir);
            if (planes[i].dotProductNormal(positionedLight.lightDir)>=0.0)
            {
                OSG_INFO<<"     Need remove side "<<i<<std::endl;
            }
            else
            {
                OSG_INFO<<std::endl;
                lightVolumePolytope.add(planes[i]);
                result_mask = result_mask | selector_mask;
            }
        }

        OSG_INFO<<"    planes.size() = "<<planes.size()<<std::endl;
        OSG_INFO<<"    planes.getResultMask() = "<<polytope.getResultMask()<<std::endl;
        OSG_INFO<<"    resultMask = "<<result_mask<<std::endl;
        polytope.setResultMask(result_mask);
    }
    else
    {
        const osg::Polytope::PlaneList& planes = polytope.getPlaneList();
        osg::Polytope::ClippingMask selector_mask = 0x1;
        osg::Polytope::ClippingMask result_mask = 0x0;
        for(unsigned int i=0; i<planes.size(); ++i, selector_mask <<= 1)
        {

            double d = planes[i].distance(positionedLight.lightPos3);
            OSG_INFO<<"      plane "<<planes[i]<<"  planes["<<i<<"].distance(lightPos3)="<<d;
            if (d<0.0)
            {
                OSG_INFO<<"     Need remove side "<<i<<std::endl;
            }
            else
            {
                OSG_INFO<<std::endl;
                lightVolumePolytope.add(planes[i]);
                result_mask = result_mask | selector_mask;
            }
        }
        OSG_INFO<<"    planes.size() = "<<planes.size()<<std::endl;
        OSG_INFO<<"    planes.getResultMask() = "<<polytope.getResultMask()<<std::endl;
        OSG_INFO<<"    resultMask = "<<result_mask<<std::endl;
        polytope.setResultMask(result_mask);
    }

    OSG_INFO<<"Which frustum edges are active?"<<std::endl;
    for(unsigned int i=0; i<12; ++i)
    {
        Frustum::Indices& indices = frustum.edges[i];

        unsigned int corner_a = indices[0];
        unsigned int corner_b = indices[1];
        unsigned int face_a = indices[2];
        unsigned int face_b = indices[3];
        bool face_a_active = (polytope.getResultMask()&(0x1<<face_a))!=0;
        bool face_b_active = (polytope.getResultMask()&(0x1<<face_b))!=0;
        unsigned int numActive = 0;
        if (face_a_active) ++numActive;
        if (face_b_active) ++numActive;
        if (numActive==1)
        {

            osg::Plane boundaryPlane;

            if (positionedLight.directionalLight)
            {
                osg::Vec3d normal = (frustum.corners[corner_b]-frustum.corners[corner_a])^positionedLight.lightDir;
                normal.normalize();
                boundaryPlane.set(normal, frustum.corners[corner_a]);
            }
            else
            {
                boundaryPlane.set(positionedLight.lightPos3, frustum.corners[corner_a], frustum.corners[corner_b]);
            }

            OSG_INFO<<"Boundary Edge "<<i<<", corner_a="<<corner_a<<", corner_b="<<corner_b<<", face_a_active="<<face_a_active<<", face_b_active="<<face_b_active;
            if (boundaryPlane.distance(frustum.center)<0.0)
            {
                boundaryPlane.flip();
                OSG_INFO<<", flipped boundary edge "<<boundaryPlane<<std::endl;
            }
            else
            {
                OSG_INFO<<", no need to flip boundary edge "<<boundaryPlane<<std::endl;
            }
            lightVolumePolytope.add(boundaryPlane);
        }
        else OSG_INFO<<"Internal Edge "<<i<<", corner_a="<<corner_a<<", corner_b="<<corner_b<<", face_a_active="<<face_a_active<<", face_b_active="<<face_b_active<<std::endl;
    }


    const osg::Polytope::PlaneList& planes = lightVolumePolytope.getPlaneList();
    for(unsigned int i=0; i<planes.size(); ++i)
    {
        OSG_INFO<<"      plane "<<planes[i]<<"  "<<((lightVolumePolytope.getResultMask() & (0x1<<i))?"on":"off")<<std::endl;
    }

    return lightVolumePolytope;
}

bool MWShadowTechnique::computeShadowCameraSettings(Frustum& frustum, LightData& positionedLight, osg::Matrixd& projectionMatrix, osg::Matrixd& viewMatrix)
{
    OSG_INFO<<"standardShadowMapCameraSettings()"<<std::endl;

    osg::Vec3d lightSide;

    const ShadowSettings* settings = getShadowedScene()->getShadowSettings();

    double dotProduct_v = positionedLight.lightDir * frustum.frustumCenterLine;
    double gamma_v = acos(dotProduct_v);
    if (gamma_v<osg::DegreesToRadians(settings->getPerspectiveShadowMapCutOffAngle()) || gamma_v>osg::DegreesToRadians(180.0-settings->getPerspectiveShadowMapCutOffAngle()))
    {
        OSG_INFO<<"View direction and Light direction below tolerance"<<std::endl;
        osg::Vec3d viewSide = osg::Matrixd::transform3x3(frustum.modelViewMatrix, osg::Vec3d(1.0,0.0,0.0));
        // OSG's `^` is the cross product, not a redundant exclusive or.
        // NOLINTNEXTLINE(misc-redundant-expression)
        lightSide = positionedLight.lightDir ^ (viewSide ^ positionedLight.lightDir);
        lightSide.normalize();
    }
    else
    {
        lightSide = positionedLight.lightDir ^ frustum.frustumCenterLine;
        lightSide.normalize();
    }

    osg::Vec3d lightUp = lightSide ^ positionedLight.lightDir;



    if (positionedLight.directionalLight)
    {
        double xMin=0.0, xMax=0.0;
        double yMin=0.0, yMax=0.0;
        double zMin=0.0, zMax=0.0;

        for(auto itr = frustum.corners.begin();
            itr != frustum.corners.end();
            ++itr)
        {
            osg::Vec3d cornerDelta(*itr - frustum.center);
            osg::Vec3d cornerInLightCoords(cornerDelta*lightSide,
                                           cornerDelta*lightUp,
                                           cornerDelta*positionedLight.lightDir);

            OSG_INFO<<"    corner ="<<*itr<<" in lightcoords "<<cornerInLightCoords<<std::endl;

            xMin = osg::minimum( xMin, cornerInLightCoords.x());
            xMax = osg::maximum( xMax, cornerInLightCoords.x());
            yMin = osg::minimum( yMin, cornerInLightCoords.y());
            yMax = osg::maximum( yMax, cornerInLightCoords.y());
            zMin = osg::minimum( zMin, cornerInLightCoords.z());
            zMax = osg::maximum( zMax, cornerInLightCoords.z());
        }

        OSG_INFO<<"before bs xMin="<<xMin<<", xMax="<<xMax<<", yMin="<<yMin<<", yMax="<<yMax<<", zMin="<<zMin<<", zMax="<<zMax<<std::endl;

        osg::BoundingSphere bs = _shadowedScene->getBound();
        osg::Vec3d modelCenterRelativeFrustumCenter(bs.center()-frustum.center);
        osg::Vec3d modelCenterInLightCoords(modelCenterRelativeFrustumCenter*lightSide,
                                            modelCenterRelativeFrustumCenter*lightUp,
                                            modelCenterRelativeFrustumCenter*positionedLight.lightDir);

        OSG_INFO<<"modelCenterInLight="<<modelCenterInLightCoords<<" radius="<<bs.radius()<<std::endl;
        double radius(bs.radius());

        xMin = osg::maximum(xMin, modelCenterInLightCoords.x()-radius);
        xMax = osg::minimum(xMax, modelCenterInLightCoords.x()+radius);
        yMin = osg::maximum(yMin, modelCenterInLightCoords.y()-radius);
        yMax = osg::minimum(yMax, modelCenterInLightCoords.y()+radius);
        zMin = modelCenterInLightCoords.z()-radius;
        zMax = osg::minimum(zMax, modelCenterInLightCoords.z()+radius);

        OSG_INFO<<"after bs xMin="<<xMin<<", xMax="<<xMax<<", yMin="<<yMin<<", yMax="<<yMax<<", zMin="<<zMin<<", zMax="<<zMax<<std::endl;

        if (xMin>=xMax || yMin>=yMax || zMin>=zMax)
        {
            OSG_INFO<<"Warning nothing available to create shadows"<<zMax<<std::endl;
            return false;
        }
        else
        {
            projectionMatrix.makeOrtho(xMin,xMax, yMin, yMax,0.0,zMax-zMin);
            viewMatrix.makeLookAt(frustum.center+positionedLight.lightDir*zMin, frustum.center+positionedLight.lightDir*zMax, lightUp);
        }
    }
    else
    {
        double zMax=-sDoubleMost;

        OSG_INFO<<"lightDir = "<<positionedLight.lightDir<<std::endl;
        OSG_INFO<<"lightPos3 = "<<positionedLight.lightPos3<<std::endl;
        for(auto & corner : frustum.corners)
        {
            osg::Vec3d cornerDelta(corner - positionedLight.lightPos3);
            osg::Vec3d cornerInLightCoords(cornerDelta*lightSide,
                                           cornerDelta*lightUp,
                                           cornerDelta*positionedLight.lightDir);

            OSG_INFO<<"   cornerInLightCoords= "<<cornerInLightCoords<<std::endl;

            zMax = osg::maximum( zMax, cornerInLightCoords.z());
        }

        OSG_INFO<<"zMax = "<<zMax<<std::endl;

        if (zMax<0.0)
        {
            // view frustum entirely behind light
            return false;
        }

        double minRatio = 0.0001;
        double zMin=zMax*minRatio;

        double fov = 180.0;
        if(fov < 180.0)   // spotlight
        {
            projectionMatrix.makePerspective(fov, 1.0, zMin, zMax);
            viewMatrix.makeLookAt(positionedLight.lightPos3,
                                          positionedLight.lightPos3+positionedLight.lightDir, lightUp);
        }
        else
        {
            double fovMAX = 160.0f;
            fov = 0.0;

            // calculate the max FOV from the corners of the frustum relative to the light position
            for(auto & corner : frustum.corners)
            {
                osg::Vec3d cornerDelta(corner - positionedLight.lightPos3);
                double length = cornerDelta.length();

                if (length==0.0) fov = osg::minimum(fov, 180.0);
                else
                {
                    double dotProduct = cornerDelta*positionedLight.lightDir;
                    double angle = 2.0*osg::RadiansToDegrees( acos(dotProduct/length) );
                    fov = osg::maximum(fov, angle);
                }
            }

            OSG_INFO<<"Computed fov = "<<fov<<std::endl;

            if (fov>fovMAX)
            {
                OSG_INFO<<"Clampping fov = "<<fov<<std::endl;
                fov=fovMAX;
            }

            projectionMatrix.makePerspective(fov, 1.0, zMin, zMax);
            viewMatrix.makeLookAt(positionedLight.lightPos3,
                                  positionedLight.lightPos3+positionedLight.lightDir, lightUp);

        }
    }
    return true;
}

namespace
{
struct ConvexHull
{
    typedef std::vector<osg::Vec3d> Vertices;
    typedef std::pair<osg::Vec3d, osg::Vec3d> Edge;
    typedef std::vector<Edge> Edges;
    typedef std::vector<osg::Vec3d> VertexSet;

    Edges _edges;

    bool valid() const { return !_edges.empty(); }

    void setToFrustum(MWShadowTechnique::Frustum& frustum)
    {
        _edges.emplace_back(frustum.corners[0], frustum.corners[1]);
        _edges.emplace_back(frustum.corners[1], frustum.corners[2]);
        _edges.emplace_back(frustum.corners[2], frustum.corners[3]);
        _edges.emplace_back(frustum.corners[3], frustum.corners[0]);

        _edges.emplace_back(frustum.corners[4], frustum.corners[5]);
        _edges.emplace_back(frustum.corners[5], frustum.corners[6]);
        _edges.emplace_back(frustum.corners[6], frustum.corners[7]);
        _edges.emplace_back(frustum.corners[7], frustum.corners[4]);

        _edges.emplace_back(frustum.corners[0], frustum.corners[4]);
        _edges.emplace_back(frustum.corners[1], frustum.corners[5]);
        _edges.emplace_back(frustum.corners[2], frustum.corners[6]);
        _edges.emplace_back(frustum.corners[3], frustum.corners[7]);
    }

    struct ConvexHull2D
    {
        // Implementation based on https://en.wikibooks.org/wiki/Algorithm_Implementation/Geometry/Convex_hull/Monotone_chain#C++
        typedef osg::Vec3d Point;

        static double cross(const Point &O, const Point &A, const Point &B)
        {
            return (A.x() - O.x())*(B.y() - O.y()) - (A.y() - O.y())*(B.x() - O.x());
        }

        // Calculates the 2D convex hull and returns it as a vector containing the points in CCW order with the first and last point being the same.
        static Vertices convexHull(const VertexSet &P)
        {
            size_t n = P.size(), k = 0;
            if (n <= 3)
                return Vertices(P.cbegin(), P.cend());

            Vertices H(2 * n);

            // Points are already sorted in a std::set

            // Build lower hull
            for(const auto& vert : P)
            {
                while (k >= 2 && cross(H[k - 2], H[k - 1], vert) <= 0)
                    k--;
                H[k++] = vert;
            }

            // Build upper hull
            size_t t = k + 1;
            for (auto pItr = std::next(P.crbegin()); pItr != P.crend(); ++pItr)
            {
                while (k >= t && cross(H[k - 2], H[k - 1], *pItr) <= 0)
                    k--;
                H[k++] = *pItr;
            }

            H.resize(k - 1);
            return H;
        }
    };

    Vertices findInternalEdges(const osg::Vec3d& mainVertex, const Vertices& connectedVertices)
    {
        Vertices internalEdgeVertices;
        for (const auto& vertex : connectedVertices)
        {
            osg::Matrixd matrix;
            osg::Vec3d dir = vertex - mainVertex;
            matrix.makeLookAt(mainVertex, vertex, dir.z() == 0 ? osg::Vec3d(0, 0, 1) : osg::Vec3d(1, 0, 0));
            Vertices testVertices;
            for (const auto& testVertex : connectedVertices)
            {
                if (vertex != testVertex)
                    testVertices.push_back(testVertex);
            }
            std::vector<double> bearings;
            for (const auto& testVertex : testVertices)
            {
                osg::Vec3d transformedVertex = testVertex * matrix;
                bearings.push_back(atan2(transformedVertex.y(), transformedVertex.x()));
            }
            std::sort(bearings.begin(), bearings.end());
            bool keep = false;
            for (auto itr = bearings.begin(); itr + 1 != bearings.end(); ++itr)
            {
                if (*itr + osg::PI < *(itr + 1))
                {
                    keep = true;
                    break;
                }
            }
            if (!keep && bearings[0] + osg::PI > bearings.back())
                keep = true;
            if (!keep)
                internalEdgeVertices.push_back(vertex);
        }
        return internalEdgeVertices;
    }

    void extendTowardsNegativeZ()
    {
        // Collect the set of vertices
        VertexSet vertices;
        for (const Edge& edge : _edges)
        {
            vertices.emplace_back(edge.first);
            vertices.emplace_back(edge.second);
        }

        // Sort and make unique.
        std::sort(vertices.begin(), vertices.end());
        vertices.erase(std::unique(vertices.begin(), vertices.end()), vertices.end());

        if (vertices.empty())
            return;

        // Get the vertices contributing to the 2D convex hull
        Vertices extremeVertices = ConvexHull2D::convexHull(vertices);

        // Add their extrusions to the final edge collection
        // We extrude as far as -1.5 as the coordinate space shouldn't ever put any shadow casters further than -1.0
        Edges finalEdges;
        // Add edges towards -Z
        for (const auto& vertex : extremeVertices)
            finalEdges.emplace_back(vertex, osg::Vec3d(vertex.x(), vertex.y(), -1.5));
        // Add edge loop to 'seal' the hull
        for (auto itr = extremeVertices.cbegin(); itr != extremeVertices.cend() - 1; ++itr)
            finalEdges.emplace_back(osg::Vec3d(itr->x(), itr->y(), -1.5), osg::Vec3d((itr + 1)->x(), (itr + 1)->y(), -1.5));
        // The convex hull algorithm we are using sometimes places a point at both ends of the vector, so we don't always need to add the last edge separately.
        if (extremeVertices.front() != extremeVertices.back())
            finalEdges.emplace_back(osg::Vec3d(extremeVertices.front().x(), extremeVertices.front().y(), -1.5), osg::Vec3d(extremeVertices.back().x(), extremeVertices.back().y(), -1.5));

        // Remove internal edges connected to extreme vertices
        for (const auto& vertex : extremeVertices)
        {
            Vertices connectedVertices;
            for (const Edge& edge : _edges)
            {
                if (edge.first == vertex)
                    connectedVertices.push_back(edge.second);
                else if (edge.second == vertex)
                    connectedVertices.push_back(edge.first);
            }
            connectedVertices.emplace_back(vertex.x(), vertex.y(), -1.5);

            Vertices unwantedEdgeEnds = findInternalEdges(vertex, connectedVertices);
            for (const auto& edgeEnd : unwantedEdgeEnds)
            {
                const auto edgeA = Edge(vertex, edgeEnd);
                const auto edgeB = Edge(edgeEnd, vertex);
                _edges.erase(std::remove_if(_edges.begin(), _edges.end(), [&](const auto& elem)
                    {
                        return elem == edgeA || elem == edgeB;
                    }), _edges.end());
            }
        }

        // Gather connected vertices
        VertexSet unprocessedConnectedVertices = std::move(extremeVertices);

        VertexSet connectedVertices;
        const auto containsVertex = [&](const auto& vert)
        {
            return std::find(connectedVertices.begin(), connectedVertices.end(), vert) != connectedVertices.end();
        };

        while (!unprocessedConnectedVertices.empty())
        {
            osg::Vec3d vertex = unprocessedConnectedVertices.back();
            unprocessedConnectedVertices.pop_back();

            connectedVertices.emplace_back(vertex);
            for (const Edge& edge : _edges)
            {
                osg::Vec3d otherEnd;
                if (edge.first == vertex)
                    otherEnd = edge.second;
                else if (edge.second == vertex)
                    otherEnd = edge.first;
                else
                    continue;

                if (containsVertex(otherEnd))
                    continue;

                unprocessedConnectedVertices.emplace_back(otherEnd);
            }
        }

        for (const Edge& edge : _edges)
        {
            if (containsVertex(edge.first) || containsVertex(edge.second))
                finalEdges.push_back(edge);
        }

        _edges = std::move(finalEdges);
    }

    void transform(const osg::Matrixd& m)
    {
        for (auto& edge : _edges)
        {
            edge.first = edge.first * m;
            edge.second = edge.second * m;
        }
    }

    void clip(const osg::Plane& plane)
    {
        Vertices intersections;

        // OSG_NOTICE<<"clip("<<plane<<") edges.size()="<<_edges.size()<<std::endl;
        for(auto itr = _edges.begin(); itr != _edges.end();)
        {
            double d0 = plane.distance(itr->first);
            double d1 = plane.distance(itr->second);
            if (d0<0.0 && d1<0.0)
            {
                // OSG_NOTICE<<"  Edge completely outside, removing"<<std::endl;
                itr = _edges.erase(itr);
            }
            else if (d0>=0.0 && d1>=0.0)
            {
                // OSG_NOTICE<<"  Edge completely inside"<<std::endl;
                ++itr;
            }
            else
            {
                osg::Vec3d& v0 = itr->first;
                osg::Vec3d& v1 = itr->second;
                osg::Vec3d intersection = v0 - (v1-v0)*(d0/(d1-d0));
                intersections.push_back(intersection);
                // OSG_NOTICE<<"  Edge across clip plane, v0="<<v0<<", v1="<<v1<<", intersection= "<<intersection<<std::endl;
                if (d0<0.0)
                {
                    // move first vertex on edge
                    v0 = intersection;
                }
                else
                {
                    // move second vertex on edge
                    v1 = intersection;
                }

                ++itr;
            }
        }
        // OSG_NOTICE<<"After clipping, have "<<intersections.size()<<" to insert"<<std::endl;

        if (intersections.size() < 2)
        {
            return;
        }

        if (intersections.size() == 2)
        {
            _edges.emplace_back(intersections[0], intersections[1]);
            return;
        }

        if (intersections.size() == 3)
        {
            _edges.emplace_back(intersections[0], intersections[1]);
            _edges.emplace_back(intersections[1], intersections[2]);
            _edges.emplace_back(intersections[2], intersections[0]);
            return;
        }

        // more than 3 intersections so have to sort them in clockwise order so that
        // we can generate the edges correctly.

        osg::Vec3d normal(plane.getNormal());

        osg::Vec3d side_x = osg::Vec3d(1.0,0.0,0.0) ^ normal;
        osg::Vec3d side_y = osg::Vec3d(0.0,1.0,0.0) ^ normal;
        osg::Vec3d side = (side_x.length2()>=side_y.length2()) ? side_x : side_y;
        side.normalize();

        osg::Vec3d up = side ^ normal;
        up.normalize();

        osg::Vec3d center;
        for(auto& vertex : intersections)
        {
            center += vertex;

            center.x() = osg::maximum(center.x(), -sDoubleMost);
            center.y() = osg::maximum(center.y(), -sDoubleMost);
            center.z() = osg::maximum(center.z(), -sDoubleMost);

            center.x() = osg::minimum(center.x(), sDoubleMost);
            center.y() = osg::minimum(center.y(), sDoubleMost);
            center.z() = osg::minimum(center.z(), sDoubleMost);
        }

        center /= static_cast<double>(intersections.size());

        typedef std::map<double, std::list<std::pair<osg::Vec3d, double>>> VertexMap;
        VertexMap vertexMap;
        for (const auto& vertex : intersections)
        {
            osg::Vec3d dv = vertex - center;
            double h = dv * side;
            double v = dv * up;
            double angle = atan2(h,v);
            // OSG_NOTICE<<"angle = "<<osg::RadiansToDegrees(angle)<<", h="<<h<<" v= "<<v<<std::endl;

            // We need to make sure all intersections are added to the list in the right order, even if they're so far away that their angles work out the same.
            double sortValue = NAN;
            if (angle < osg::DegreesToRadians(-135.0) || angle > osg::DegreesToRadians(135.0))
                sortValue = -h;
            else if (angle < osg::DegreesToRadians(-45.0))
                sortValue = v;
            else if (angle < osg::DegreesToRadians(45.0))
                sortValue = h;
            else
                sortValue = -v;
            if (vertexMap.contains(angle))
            {
                auto listItr = vertexMap[angle].begin();
                while (listItr != vertexMap[angle].end() && listItr->second < sortValue)
                    ++listItr;
                vertexMap[angle].emplace(listItr, std::make_pair(vertex, sortValue));
            }
            else
                vertexMap[angle].emplace_back(vertex, sortValue);
        }

        osg::Vec3d previous_v = vertexMap.rbegin()->second.back().first;
        for (auto & itr : vertexMap)
        {
            for (const auto& vertex : itr.second)
            {
                _edges.emplace_back(previous_v, vertex.first);
                previous_v = vertex.first;
            }
        }

        // OSG_NOTICE<<"  after clip("<<plane<<") edges.size()="<<_edges.size()<<std::endl;
    }

    void clip(const osg::Polytope& polytope)
    {
        const osg::Polytope::PlaneList& planes = polytope.getPlaneList();
        for(const auto& plane : planes)
        {
            clip(plane);
        }
    }

    double min(unsigned int index) const
    {
        double m = sDoubleMost;
        for(const auto& edge : _edges)
        {
            m = std::min(edge.first[static_cast<int>(index)], m);
            m = std::min(edge.second[static_cast<int>(index)], m);
        }
        return m;
    }

    double max(unsigned int index) const
    {
        double m = -sDoubleMost;
        for (const auto& edge : _edges)
        {
            m = std::max(edge.first[static_cast<int>(index)], m);
            m = std::max(edge.second[static_cast<int>(index)], m);
        }
        return m;
    }

    double minRatio(const osg::Vec3d& eye, unsigned int index) const
    {
        double m = sDoubleMost;
        osg::Vec3d delta;
        double ratio = NAN;
        for (const auto& edge : _edges)
        {
            delta = edge.first - eye;
            ratio = delta[static_cast<int>(index)] / delta[1];
            m = std::min(ratio, m);

            delta = edge.second - eye;
            ratio = delta[static_cast<int>(index)] / delta[1];
            m = std::min(ratio, m);
        }
        return m;
    }

    double maxRatio(const osg::Vec3d& eye, unsigned int index) const
    {
        double m = -sDoubleMost;
        osg::Vec3d delta;
        double ratio = NAN;
        for (const auto& edge : _edges)
        {
            delta = edge.first - eye;
            ratio = delta[static_cast<int>(index)] / delta[1];
            m = std::max(ratio, m);

            delta = edge.second - eye;
            ratio = delta[static_cast<int>(index)] / delta[1];
            m = std::max(ratio, m);
        }
        return m;
    }

    void output(std::ostream& out)
    {
        out << "ConvexHull" << std::endl;
        for (const auto& edge : _edges)
        {
            out << "   edge (" << edge.first << ") (" << edge.second << ")" << std::endl;
        }
    }
};


struct RenderLeafBounds
{
    RenderLeafBounds():
        computeRatios(false),
        numRenderLeaf(0),
        n(0.0),
        previous_modelview(nullptr),
        clip_min_x(-1.0), clip_max_x(1.0),
        clip_min_y(-1.0), clip_max_y(1.0),
        clip_min_z(-1.0), clip_max_z(1.0),
        clip_min_x_ratio(-sDoubleMost), clip_max_x_ratio(sDoubleMost),
        clip_min_z_ratio(-sDoubleMost), clip_max_z_ratio(sDoubleMost),
        min_x_ratio(sDoubleMost), max_x_ratio(-sDoubleMost),
        min_z_ratio(sDoubleMost), max_z_ratio(-sDoubleMost),
        min_x(1.0), max_x(-1.0),
        min_y(1.0), max_y(-1.0),
        min_z(1.0), max_z(-1.0)
    {
        //OSG_NOTICE<<std::endl<<"RenderLeafBounds"<<std::endl;
    }

    void set(const osg::Matrixd& p)
    {
        computeRatios = false;
        light_p = p;
        clip_min_x = -sDoubleMost; clip_max_x = sDoubleMost;
        clip_min_y = -sDoubleMost; clip_max_y = sDoubleMost;
        clip_min_z = -sDoubleMost; clip_max_z = sDoubleMost;
        min_x = sDoubleMost; max_x = -sDoubleMost;
        min_y = sDoubleMost; max_y = -sDoubleMost;
        min_z = sDoubleMost; max_z = -sDoubleMost;
    }

    void set(const osg::Matrixd& p, osg::Vec3d& e_ls, double nr)
    {
        computeRatios = true;
        light_p = p;
        eye_ls = e_ls;
        n = nr;
    }

    void operator() (const osgUtil::RenderLeaf* renderLeaf)
    {
        ++numRenderLeaf;

        if (renderLeaf->_modelview.get()!=previous_modelview)
        {
            previous_modelview = renderLeaf->_modelview.get();
            if (previous_modelview)
            {
                light_mvp.mult(*renderLeaf->_modelview, light_p);
            }
            else
            {
                // no modelview matrix (such as when LightPointNode is in the scene graph) so assume
                // that modelview matrix is indentity.
                light_mvp = light_p;
            }
            // OSG_INFO<<"Computing new light_mvp "<<light_mvp<<std::endl;
        }
        else
        {
            // OSG_INFO<<"Reusing light_mvp "<<light_mvp<<std::endl;
        }

        const osg::BoundingBox& bb = renderLeaf->_drawable->getBoundingBox();
        if (bb.valid())
        {
            // OSG_NOTICE<<"checked extents of "<<renderLeaf->_drawable->getName()<<std::endl;
            handle(osg::Vec3d(bb.xMin(),bb.yMin(),bb.zMin()));
            handle(osg::Vec3d(bb.xMax(),bb.yMin(),bb.zMin()));
            handle(osg::Vec3d(bb.xMin(),bb.yMax(),bb.zMin()));
            handle(osg::Vec3d(bb.xMax(),bb.yMax(),bb.zMin()));
            handle(osg::Vec3d(bb.xMin(),bb.yMin(),bb.zMax()));
            handle(osg::Vec3d(bb.xMax(),bb.yMin(),bb.zMax()));
            handle(osg::Vec3d(bb.xMin(),bb.yMax(),bb.zMax()));
            handle(osg::Vec3d(bb.xMax(),bb.yMax(),bb.zMax()));
        }
        else
        {
            OSG_INFO<<"bb invalid"<<std::endl;
        }
    }

    void handle(const osg::Vec3d& v)
    {
        osg::Vec3d ls = v * light_mvp;

        // OSG_NOTICE<<"   corner v="<<v<<", ls="<<ls<<std::endl;

        if (computeRatios)
        {
            osg::Vec3d delta = ls-eye_ls;

            double x_ratio = NAN, z_ratio = NAN;
            if (delta.y()>n)
            {
                x_ratio = delta.x()/delta.y();
                z_ratio = delta.z()/delta.y();
            }
            else
            {
                x_ratio = delta.x()/n;
                z_ratio = delta.z()/n;
            }

            min_x_ratio = std::min(x_ratio, min_x_ratio);
            max_x_ratio = std::max(x_ratio, max_x_ratio);
            min_z_ratio = std::min(z_ratio, min_z_ratio);
            max_z_ratio = std::max(z_ratio, max_z_ratio);
        }

        // clip to the light space
        ls.x() = std::max(ls.x(), clip_min_x);
        ls.x() = std::min(ls.x(), clip_max_x);
        ls.y() = std::max(ls.y(), clip_min_y);
        ls.y() = std::min(ls.y(), clip_max_y);
        ls.z() = std::max(ls.z(), clip_min_z);
        ls.z() = std::min(ls.z(), clip_max_z);

        // compute the xyz range.
        min_x = std::min(ls.x(), min_x);
        max_x = std::max(ls.x(), max_x);
        min_y = std::min(ls.y(), min_y);
        max_y = std::max(ls.y(), max_y);
        min_z = std::min(ls.z(), min_z); /* OSG_NOTICE<<" - ";*/ 
        max_z = std::max(ls.z(), max_z); /* OSG_NOTICE<<" + ";*/ 

        // OSG_NOTICE<<"   bb.z() in ls = "<<ls.z()<<std::endl;

    }

    bool                computeRatios;

    unsigned int        numRenderLeaf;

    osg::Matrixd        light_p;
    osg::Vec3d          eye_ls;
    double              n;

    osg::Matrixd        light_mvp;
    osg::RefMatrix*     previous_modelview;

    double clip_min_x, clip_max_x;
    double clip_min_y, clip_max_y;
    double clip_min_z, clip_max_z;

    double clip_min_x_ratio, clip_max_x_ratio;
    double clip_min_z_ratio, clip_max_z_ratio;

    double min_x_ratio, max_x_ratio;
    double min_z_ratio, max_z_ratio;
    double min_x, max_x;
    double min_y, max_y;
    double min_z, max_z;
};
}


bool MWShadowTechnique::cropShadowCameraToMainFrustum(Frustum& frustum, osg::Camera* camera, double viewNear, double viewFar, std::vector<osg::Plane>& planeList)
{
    osg::Matrixd light_p = camera->getProjectionMatrix();
    osg::Matrixd light_v = camera->getViewMatrix();
    osg::Matrixd light_vp = light_v * light_p;
    osg::Matrixd oldLightP = light_p;
    
    ConvexHull convexHull;
    convexHull.setToFrustum(frustum);

    osg::Vec3d nearPoint = frustum.eye + frustum.frustumCenterLine * viewNear;
    osg::Vec3d farPoint = frustum.eye + frustum.frustumCenterLine * viewFar;

    double nearDist = -frustum.frustumCenterLine * nearPoint;
    double farDist = frustum.frustumCenterLine * farPoint;

    convexHull.clip(osg::Plane(frustum.frustumCenterLine, nearDist));
    convexHull.clip(osg::Plane(-frustum.frustumCenterLine, farDist));

    convexHull.transform(light_vp);

    double xMin = -1.0, xMax = 1.0;
    double yMin = -1.0, yMax = 1.0;
    double zMin = -1.0, zMax = 1.0;

    if (convexHull.valid())
    {
        xMin = osg::maximum(-1.0, convexHull.min(0));
        xMax = osg::minimum(1.0, convexHull.max(0));
        yMin = osg::maximum(-1.0, convexHull.min(1));
        yMax = osg::minimum(1.0, convexHull.max(1));
        zMin = osg::maximum(-1.0, convexHull.min(2));
        zMax = osg::minimum(1.0, convexHull.max(2));
    }
    else
        return false;

    if (xMin != -1.0 || yMin != -1.0 || zMin != -1.0 ||
        xMax != 1.0 || yMax != 1.0 || zMax != 1.0)
    {
        osg::Matrix m;
        m.makeTranslate(osg::Vec3d(-0.5*(xMax + xMin),
                                   -0.5*(yMax + yMin),
                                   -0.5*(zMax + zMin)));

        m.postMultScale(osg::Vec3d(2.0 / (xMax - xMin),
                                   2.0 / (yMax - yMin),
                                   2.0 / (zMax - zMin)));

        light_p.postMult(m);
        camera->setProjectionMatrix(light_p);

        convexHull.transform(osg::Matrixd::inverse(oldLightP));

        xMin = convexHull.min(0);
        xMax = convexHull.max(0);
        yMin = convexHull.min(1);
        yMax = convexHull.max(1);
        zMin = convexHull.min(2);

        planeList.emplace_back(0.0, -1.0, 0.0, yMax);
        planeList.emplace_back(0.0, 1.0, 0.0, -yMin);
        planeList.emplace_back(-1.0, 0.0, 0.0, xMax);
        planeList.emplace_back(1.0, 0.0, 0.0, -xMin);
        // In view space, the light is at the most positive value, and we want to cull stuff beyond the minimum value.
        planeList.emplace_back(0.0, 0.0, 1.0, -zMin);
        // Don't add a zMax culling plane - we still want those objects, but don't care about their depth buffer value.
    }

    return true;
}

bool MWShadowTechnique::adjustPerspectiveShadowMapCameraSettings(osgUtil::RenderStage* renderStage, Frustum& frustum, LightData& /*positionedLight*/, osg::Camera* camera, double viewNear, double viewFar)
{
    const ShadowSettings* settings = getShadowedScene()->getShadowSettings();

    //frustum.projectionMatrix;
    //frustum.modelViewMatrix;

    osg::Matrixd light_p = camera->getProjectionMatrix();
    osg::Matrixd light_v = camera->getViewMatrix();
    osg::Matrixd light_vp = light_v * light_p;
    osg::Vec3d lightdir(0.0,0.0,-1.0);

    // check whether this light space projection is perspective or orthographic.
    bool orthographicLightSpaceProjection = light_p(0,3)==0.0 && light_p(1,3)==0.0 && light_p(2,3)==0.0;

    if (!orthographicLightSpaceProjection)
    {
        OSG_INFO<<"perspective light space projection not yet supported."<<std::endl;
        return false;
    }


    //OSG_NOTICE<<"light_v="<<light_v<<std::endl;
    //OSG_NOTICE<<"light_p="<<light_p<<std::endl;

    ConvexHull convexHull;
    convexHull.setToFrustum(frustum);

    osg::Vec3d nearPoint = frustum.eye + frustum.frustumCenterLine * viewNear;
    osg::Vec3d farPoint = frustum.eye + frustum.frustumCenterLine * viewFar;

    double nearDist = -frustum.frustumCenterLine * nearPoint;
    double farDist = frustum.frustumCenterLine * farPoint;

    convexHull.clip(osg::Plane(frustum.frustumCenterLine, nearDist));
    convexHull.clip(osg::Plane(-frustum.frustumCenterLine, farDist));


    convexHull.transform(light_vp);

    ConvexHull convexHullUnextended = convexHull;

    convexHull.extendTowardsNegativeZ();

    {
        // extendTowardsNegativeZ is too numerically unstable to do this first
        convexHull.clip(osg::Plane(-1.0,0.0,0.0,1.0));
        convexHull.clip(osg::Plane(1.0,0.0,0.0,1.0));
        convexHull.clip(osg::Plane(0.0,-1.0,0.0,1.0));
        convexHull.clip(osg::Plane(0.0,1.0,0.0,1.0));
        convexHullUnextended.clip(osg::Plane(-1.0,0.0,0.0,1.0));
        convexHullUnextended.clip(osg::Plane(1.0,0.0,0.0,1.0));
        convexHullUnextended.clip(osg::Plane(0.0,-1.0,0.0,1.0));
        convexHullUnextended.clip(osg::Plane(0.0,1.0,0.0,1.0));
    }





    double xMin=-1.0, xMax=1.0;
    double yMin=-1.0, yMax=1.0;
    double zMin=-1.0, zMax=1.0;

    if (convexHull.valid())
    {
        double widest_x = osg::maximum(fabs(convexHull.min(0)), fabs(convexHull.max(0)));
        xMin = osg::maximum(-1.0,-widest_x);
        xMax = osg::minimum(1.0,widest_x);
        yMin = osg::maximum(-1.0,convexHull.min(1));
        yMax = osg::minimum(1.0,convexHull.max(1));
    }
    else
    {
        // clipping of convex hull has invalidated it, so reset it so later checks on it provide valid results.
        convexHull.setToFrustum(frustum);
        convexHull.transform(light_vp);
    }


    // we always want the lightspace to include the computed near plane.
    zMin = -1.0;
    if (xMin!=-1.0 || yMin!=-1.0 || zMin!=-1.0 ||
        xMax!=1.0 || yMax!=1.0 || zMax!=1.0)
    {
        osg::Matrix m;
        m.makeTranslate(osg::Vec3d(-0.5*(xMax+xMin),
                                    -0.5*(yMax+yMin),
                                    -0.5*(zMax+zMin)));

        m.postMultScale(osg::Vec3d(2.0/(xMax-xMin),
                                   2.0/(yMax-yMin),
                                   2.0/(zMax-zMin)));

        convexHull.transform(m);
        convexHullUnextended.transform(m);
        light_p.postMult(m);
        light_vp = light_v * light_p;

        camera->setProjectionMatrix(light_p);
    }


    osg::Vec3d eye_v = frustum.eye * light_v;
    //osg::Vec3d centerNearPlane_v = frustum.centerNearPlane * light_v;
    osg::Vec3d center_v = frustum.center * light_v;
    osg::Vec3d viewdir_v = center_v-eye_v; viewdir_v.normalize();

    double dotProduct_v = lightdir * viewdir_v;
    double gamma_v = acos(dotProduct_v);
    if (gamma_v<osg::DegreesToRadians(settings->getPerspectiveShadowMapCutOffAngle()) || gamma_v>osg::DegreesToRadians(180-settings->getPerspectiveShadowMapCutOffAngle()))
    {
        // OSG_NOTICE<<"Light and view vectors near parallel - use standard shadow map."<<std::endl;
        return true;
    }

    //OSG_NOTICE<<"gamma="<<osg::RadiansToDegrees(gamma_v)<<std::endl;
    //OSG_NOTICE<<"eye_v="<<eye_v<<std::endl;
    //OSG_NOTICE<<"viewdir_v="<<viewdir_v<<std::endl;

    osg::Vec3d eye_ls = frustum.eye * light_vp;

    //osg::Vec3d centerNearPlane_ls = frustum.centerNearPlane * light_vp;
    //osg::Vec3d centerFarPlane_ls = frustum.centerFarPlane * light_vp;
    osg::Vec3d center_ls = frustum.center * light_vp;
    osg::Vec3d viewdir_ls = center_ls-eye_ls; viewdir_ls.normalize();

    osg::Vec3d side = lightdir ^ viewdir_ls; side.normalize();
    osg::Vec3d up = side ^ lightdir;

    double d = 2.0;

    double alpha = osg::DegreesToRadians(30.0);
    double n = tan(alpha)*tan(osg::PI_2-gamma_v)*tan(osg::PI_2-gamma_v);
    //double n = tan(alpha)*tan(osg::PI_2-gamma_v);

    //OSG_NOTICE<<"n = "<<n<<", eye_ls.y()="<<eye_ls.y()<<", eye_v="<<eye_v<<", eye="<<frustum.eye<<std::endl;
    double min_n = osg::maximum(-1.0-eye_ls.y(), settings->getMinimumShadowMapNearFarRatio());
    n = std::max(n, min_n); //OSG_NOTICE<<"Clamping n to eye point"<<std::endl;
        

    //n = min_n;

    //n = 0.01;

    //n = z_n;

    double f = n+d;

    double a = (f+n)/(f-n);
    double b = -2.0*f*n/(f-n);

    osg::Vec3d virtual_eye(0.0,-1.0-n, eye_ls.z());

    osg::Matrixd lightView;
    lightView.makeLookAt(virtual_eye, virtual_eye+lightdir, up);


    double min_x_ratio = 0.0;
    double max_x_ratio = 0.0;
    double min_z_ratio = sDoubleMost;
    double max_z_ratio = -sDoubleMost;

    min_x_ratio = convexHull.valid() ? convexHull.minRatio(virtual_eye,0) : -sDoubleMost;
    max_x_ratio = convexHull.valid() ? convexHull.maxRatio(virtual_eye,0) : sDoubleMost;
    //min_z_ratio = convexHull.minRatio(virtual_eye,2);
    //max_z_ratio = convexHull.maxRatio(virtual_eye,2);

    if (convexHullUnextended.valid())
    {
        min_z_ratio = convexHullUnextended.minRatio(virtual_eye, 2);
        max_z_ratio = convexHullUnextended.maxRatio(virtual_eye, 2);
    }


    double best_x_ratio = osg::maximum(fabs(min_x_ratio),fabs(max_x_ratio));
    double best_z_ratio = osg::maximum(fabs(min_z_ratio),fabs(max_z_ratio));

    //best_z_ratio = osg::maximum(1.0, best_z_ratio);

    //best_z_ratio *= 10.0;

    osg::Matrixd lightPerspective( 1.0/best_x_ratio,  0.0, 0.0,  0.0,
                                   0.0,  a,   0.0,  1.0,
                                   0.0,  0.0, 1.0/best_z_ratio,  0.0,
                                   0.0,  b,   0.0,  0.0 );
    osg::Matrixd light_persp = light_p * lightView * lightPerspective;

    if (convexHull.valid())
    {
        convexHull.transform(lightView * lightPerspective);
        xMin = osg::maximum(-1.0, convexHull.min(0));
        xMax = osg::minimum(1.0, convexHull.max(0));
        yMin = osg::maximum(-1.0, convexHull.min(1));
        yMax = osg::minimum(1.0, convexHull.max(1));

        if (xMin != -1.0 || yMin != -1.0 || xMax != 1.0 || yMax != 1.0)
        {
            osg::Matrix m;
            m.makeTranslate(osg::Vec3d(-0.5*(xMax+xMin),
                                       -0.5*(yMax+yMin),
                                       0.0));

            m.postMultScale(osg::Vec3d(2.0/(xMax-xMin),
                                       2.0/(yMax-yMin),
                                       1.0));

            convexHull.transform(m);
            convexHullUnextended.transform(m);
            light_persp.postMult(m);
        }
    }

    camera->setProjectionMatrix(light_persp);

    return true;
}

void MWShadowTechnique::assignShadowStateSettings(osgUtil::CullVisitor& cv, osg::Camera* camera, unsigned int sm_i, Uniforms& uniforms)
{
    osg::Matrix inverseViewMatrix = osg::Matrix::inverse(*cv.getModelViewMatrix());
    osg::Matrix shadowSpaceMatrix = inverseViewMatrix *
        camera->getViewMatrix() *
        camera->getProjectionMatrix() *
        osg::Matrix::translate(1.0,1.0,1.0) *
        osg::Matrix::scale(0.5,0.5,0.5);

    std::string shadowSpaceUniformName = "shadowSpaceMatrix" + std::to_string(sm_i);
    osg::ref_ptr<osg::Uniform> shadowSpaceUniform;

    for (const auto & uniform : uniforms)
    {
        if (uniform->getName() == shadowSpaceUniformName)
        {
            shadowSpaceUniform = uniform;
            break;
        }
    }

    if (!shadowSpaceUniform)
    {
        shadowSpaceUniform = new osg::Uniform(osg::Uniform::FLOAT_MAT4, shadowSpaceUniformName);
        uniforms.push_back(shadowSpaceUniform);
    }

    shadowSpaceUniform->set(shadowSpaceMatrix);
}

void SceneUtil::MWShadowTechnique::assignValidRegionSettings(osgUtil::CullVisitor & cv, osg::Camera* camera, unsigned int sm_i, Uniforms & uniforms)
{
    osg::Matrix validRegionMatrix = osg::Matrix::inverse(*cv.getModelViewMatrix()) *  camera->getViewMatrix() * camera->getProjectionMatrix();

    std::string validRegionUniformName = "validRegionMatrix" + std::to_string(sm_i);
    osg::ref_ptr<osg::Uniform> validRegionUniform;

    for (const auto & uniform : uniforms)
    {
        if (uniform->getName() == validRegionUniformName)
        {
            validRegionUniform = uniform;
            break;
        }
    }

    if (!validRegionUniform)
    {
        validRegionUniform = new osg::Uniform(osg::Uniform::FLOAT_MAT4, validRegionUniformName);
        uniforms.push_back(validRegionUniform);
    }

    validRegionUniform->set(validRegionMatrix);
}

void MWShadowTechnique::cullShadowReceivingScene(osgUtil::CullVisitor* cv) const
{
    OSG_INFO<<"cullShadowReceivingScene()"<<std::endl;

    // record the traversal mask on entry so we can reapply it later.
    unsigned int traversalMask = cv->getTraversalMask();

    cv->setTraversalMask( traversalMask & _shadowedScene->getShadowSettings()->getReceivesShadowTraversalMask() );

    _shadowedScene->osg::Group::traverse(*cv);

    cv->setTraversalMask( traversalMask );

    return;
}

void MWShadowTechnique::cullShadowCastingScene(osgUtil::CullVisitor* cv, osg::Camera* camera) const
{
    OSG_INFO<<"cullShadowCastingScene()"<<std::endl;

    // record the traversal mask on entry so we can reapply it later.
    unsigned int traversalMask = cv->getTraversalMask();

    cv->setTraversalMask( traversalMask & _shadowedScene->getShadowSettings()->getCastsShadowTraversalMask() );

        if (camera) camera->accept(*cv);

    cv->setTraversalMask( traversalMask );

    return;
}

osg::StateSet* MWShadowTechnique::prepareStateSetForRenderingShadow(ViewDependentData& vdd, unsigned int traversalNumber) const
{
    OSG_INFO<<"   prepareStateSetForRenderingShadow() "<<vdd.getStateSet(traversalNumber)<<std::endl;

    osg::ref_ptr<osg::StateSet> stateset = vdd.getStateSet(traversalNumber);

    stateset->clear();

    stateset->setTextureAttribute(0, _fallbackBaseTexture.get(), osg::StateAttribute::ON);

    for(const auto& uniform : _uniforms[traversalNumber % 2])
    {
        OSG_INFO<<"addUniform("<<uniform->getName()<<")"<<std::endl;
        stateset->addUniform(uniform);
    }

    for(const auto& uniform : vdd._uniforms[traversalNumber % 2])
    {
        OSG_INFO<<"addUniform("<<uniform->getName()<<")"<<std::endl;
        stateset->addUniform(uniform);
    }

    if (_program.valid())
    {
        stateset->setAttribute(_program.get());
    }

    LightDataList& pll = vdd.getLightDataList();
    for(auto & itr : pll)
    {
        // 3. create per light/per shadow map division of lightspace/frustum
        //    create a list of light/shadow map data structures

        LightData& pl = (*itr);

        // if no texture units have been activated for this light then no shadow state required.
        if (pl.textureUnits.empty()) continue;

        for(unsigned int & textureUnit : pl.textureUnits)
        {
            OSG_INFO<<"   Need to assign state for "<<textureUnit<<std::endl;
        }

    }

    const ShadowSettings* settings = getShadowedScene()->getShadowSettings();
    unsigned int shadowMapModeValue = settings->getUseOverrideForShadowMapTexture() ?
                                            osg::StateAttribute::ON | osg::StateAttribute::OVERRIDE :
                                            osg::StateAttribute::ON;


    ShadowDataList& sdl = vdd.getShadowDataList();
    for(auto & itr : sdl)
    {
        // 3. create per light/per shadow map division of lightspace/frustum
        //    create a list of light/shadow map data structures

        ShadowData& sd = (*itr);

        OSG_INFO<<"   ShadowData for "<<sd._textureUnit<<std::endl;

        stateset->setTextureAttribute(sd._textureUnit, sd._texture.get(), shadowMapModeValue);
    }

    return stateset;
}

void MWShadowTechnique::resizeGLObjectBuffers(unsigned int /*maxSize*/)
{
    // the way that ViewDependentData is mapped shouldn't
}

void MWShadowTechnique::releaseGLObjects(osg::State* state) const
{
    std::scoped_lock lock(_viewDependentDataMapMutex);
    for(const auto & itr : _viewDependentDataMap)
    {
        ViewDependentData* vdd = itr.second.get();
        if (vdd)
        {
            vdd->releaseGLObjects(state);
        }
    }
    if (_debugHud)
        _debugHud->releaseGLObjects(state);
}

namespace
{
class DoubleBufferCallback : public osg::Callback
{
public:
    explicit DoubleBufferCallback(osg::NodeList &children) : mChildren(children) {}

    bool run(osg::Object* node, osg::Object* visitor) override
    {
        // We can't use a static cast as NodeVisitor virtually inherits from Object
        osg::ref_ptr<osg::NodeVisitor> nodeVisitor = visitor->asNodeVisitor();
        unsigned int traversalNumber = nodeVisitor->getTraversalNumber();
        mChildren[traversalNumber % 2]->accept(*nodeVisitor);

        return true;
    }

protected:
    osg::NodeList mChildren;
};
}


SceneUtil::MWShadowTechnique::DebugHUD::DebugHUD(int numberOfShadowMapsPerLight) : mDebugProgram(new osg::Program)
{
    osg::ref_ptr<osg::Shader> vertexShader = new osg::Shader(osg::Shader::VERTEX, debugVertexShaderSource);
    mDebugProgram->addShader(vertexShader);
    osg::ref_ptr<osg::Shader> fragmentShader = new osg::Shader(osg::Shader::FRAGMENT, debugFragmentShaderSource);
    mDebugProgram->addShader(fragmentShader);

    osg::ref_ptr<osg::Program> frustumProgram = new osg::Program;
    vertexShader = new osg::Shader(osg::Shader::VERTEX, debugFrustumVertexShaderSource);
    frustumProgram->addShader(vertexShader);
    fragmentShader = new osg::Shader(osg::Shader::FRAGMENT, debugFrustumFragmentShaderSource);
    frustumProgram->addShader(fragmentShader);

    for (auto& frustumGeometry : mFrustumGeometries)
    {
        frustumGeometry = new osg::Geometry();
        frustumGeometry->setCullingActive(false);

        frustumGeometry->getOrCreateStateSet()->setAttributeAndModes(frustumProgram, osg::StateAttribute::ON);
    }

    osg::ref_ptr<osg::DrawElementsUShort> frustumDrawElements = new osg::DrawElementsUShort(osg::PrimitiveSet::LINE_STRIP);
    for (auto & geom : mFrustumGeometries)
        geom->addPrimitiveSet(frustumDrawElements);
    frustumDrawElements->push_back(0);
    frustumDrawElements->push_back(1);
    frustumDrawElements->push_back(2);
    frustumDrawElements->push_back(3);
    frustumDrawElements->push_back(0);
    frustumDrawElements->push_back(4);
    frustumDrawElements->push_back(5);
    frustumDrawElements->push_back(6);
    frustumDrawElements->push_back(7);
    frustumDrawElements->push_back(4);

    frustumDrawElements = new osg::DrawElementsUShort(osg::PrimitiveSet::LINES);
    for (auto & geom : mFrustumGeometries)
        geom->addPrimitiveSet(frustumDrawElements);
    frustumDrawElements->push_back(1);
    frustumDrawElements->push_back(5);
    frustumDrawElements->push_back(2);
    frustumDrawElements->push_back(6);
    frustumDrawElements->push_back(3);
    frustumDrawElements->push_back(7);

    for (int i = 0; i < numberOfShadowMapsPerLight; ++i)
        addAnotherShadowMap();
}

void SceneUtil::MWShadowTechnique::DebugHUD::draw(const osg::ref_ptr<osg::Texture2D>& texture, unsigned int shadowMapNumber, const osg::Matrixd &matrix, osgUtil::CullVisitor& cv)
{
    // It might be possible to change shadow settings at runtime
    if (shadowMapNumber > mDebugCameras.size())
        addAnotherShadowMap();
    
    osg::ref_ptr<osg::StateSet> stateSet = new osg::StateSet();
    stateSet->setTextureAttribute(sDebugTextureUnit, texture, osg::StateAttribute::ON);

    auto frustumUniform = mFrustumUniforms[cv.getTraversalNumber() % 2][shadowMapNumber];
    frustumUniform->set(matrix);
    stateSet->addUniform(frustumUniform);

    // Some of these calls may be superfluous.
    unsigned int traversalMask = cv.getTraversalMask();
    cv.setTraversalMask(mDebugGeometry[shadowMapNumber]->getNodeMask());
    cv.pushStateSet(stateSet);
    mDebugCameras[shadowMapNumber]->accept(cv);
    cv.popStateSet();
    cv.setTraversalMask(traversalMask);
}

void SceneUtil::MWShadowTechnique::DebugHUD::releaseGLObjects(osg::State* state) const
{
    for (auto const& camera : mDebugCameras)
        camera->releaseGLObjects(state);
    mDebugProgram->releaseGLObjects(state);
    for (auto const& node : mDebugGeometry)
        node->releaseGLObjects(state);
    for (auto const& node : mFrustumTransforms)
        node->releaseGLObjects(state);
    for (auto const& node : mFrustumGeometries)
        node->releaseGLObjects(state);
}

void SceneUtil::MWShadowTechnique::DebugHUD::setFrustumVertices(const osg::ref_ptr<osg::Vec3Array>& vertices, unsigned int traversalNumber)
{
    mFrustumGeometries[traversalNumber % 2]->setVertexArray(vertices);
}

void SceneUtil::MWShadowTechnique::DebugHUD::addAnotherShadowMap()
{
    size_t shadowMapNumber = mDebugCameras.size();

    mDebugCameras.push_back(new osg::Camera);
    mDebugCameras[shadowMapNumber]->setViewport(static_cast<int>(200 * shadowMapNumber), 0, 200, 200);
    mDebugCameras[shadowMapNumber]->setRenderOrder(osg::Camera::POST_RENDER);
    mDebugCameras[shadowMapNumber]->setClearColor(osg::Vec4(1.0, 1.0, 0.0, 1.0));
    mDebugCameras[shadowMapNumber]->getOrCreateStateSet()->setMode(GL_DEPTH_TEST, osg::StateAttribute::OFF);

    mDebugGeometry.emplace_back(osg::createTexturedQuadGeometry(osg::Vec3(-1, -1, 0), osg::Vec3(2, 0, 0), osg::Vec3(0, 2, 0)));
    mDebugGeometry[shadowMapNumber]->setCullingActive(false);
    mDebugCameras[shadowMapNumber]->addChild(mDebugGeometry[shadowMapNumber]);
    osg::ref_ptr<osg::StateSet> stateSet = mDebugGeometry[shadowMapNumber]->getOrCreateStateSet();
    stateSet->setAttributeAndModes(mDebugProgram, osg::StateAttribute::ON);
    osg::ref_ptr<osg::Uniform> textureUniform = new osg::Uniform("texture", sDebugTextureUnit);
    //textureUniform->setType(osg::Uniform::SAMPLER_2D);
    stateSet->addUniform(textureUniform.get());

    mFrustumTransforms.push_back(new osg::Group);
    osg::NodeList frustumGeometryNodeList(mFrustumGeometries.cbegin(), mFrustumGeometries.cend());
    mFrustumTransforms[shadowMapNumber]->setCullCallback(new DoubleBufferCallback(frustumGeometryNodeList));
    mFrustumTransforms[shadowMapNumber]->setCullingActive(false);
    mDebugCameras[shadowMapNumber]->addChild(mFrustumTransforms[shadowMapNumber]);

    for(auto& uniformVector : mFrustumUniforms)
        uniformVector.push_back(new osg::Uniform(osg::Uniform::FLOAT_MAT4, "transform"));
}

osg::ref_ptr<osg::StateSet> SceneUtil::MWShadowTechnique::getOrCreateShadowsBinStateSet()
{
    if (_shadowsBinStateSet == nullptr)
    {
        if (_shadowsBin == nullptr)
        {
            _shadowsBin = new ShadowsBin(_castingPrograms);
            osgUtil::RenderBin::addRenderBinPrototype(_shadowsBinName, _shadowsBin);
        }
        _shadowsBinStateSet = new osg::StateSet;
        _shadowsBinStateSet->setRenderBinDetails(osg::StateSet::OPAQUE_BIN, _shadowsBinName, osg::StateSet::OVERRIDE_PROTECTED_RENDERBIN_DETAILS);
    }
    return _shadowsBinStateSet;
}

// NOLINTEND(readability-identifier-naming)

// clang-format on
