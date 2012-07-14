#pragma once
#ifndef DS_OBJECT_INTERFACE_H
#define DS_OBJECT_INTERFACE_H
#include "cinder/Cinder.h"
#include <list>
#include "cinder/Color.h"
#include "cinder/Matrix22.h"
#include "cinder/Matrix33.h"
#include "cinder/MatrixAffine2.h"
#include "cinder/Matrix44.h"
#include "cinder/Vector.h"
#include "ds/util/bit_mask.h"
#include "ds/ui/sprite/dirty_state.h"
#include "ds/ui/touch/touch_process.h"
#include "ds/ui/touch/multi_touch_constraints.h"

using namespace ci;

namespace ds {

class UpdateParams;
class DrawParams;

namespace ui {

struct TouchInfo;
class SpriteEngine;
class SpriteRegistry;
struct DragDestinationInfo;

/*!
 * brief Base Class for App Entities
 *
 * basic scene container for app. objects implement a few functions to abstract functionality.
 * Sprite will delete children when clearing.
 */
class Sprite
{
    public:
        static void           addTo(SpriteRegistry&);

        Sprite(SpriteEngine&, float width = 0.0f, float height = 0.0f);
        virtual ~Sprite();

        // Sprite behaviour can vary whether this is running on the server
        // or client.
        virtual void        updateClient(const UpdateParams &updateParams);
        virtual void        updateServer(const UpdateParams &updateParams);

        virtual void        drawClient( const Matrix44f &trans, const DrawParams &drawParams );
        virtual void        drawServer( const Matrix44f &trans, const DrawParams &drawParams );

        virtual void        setSize(float width, float height, float depth = 1.0f);
        float               getWidth() const;
        float               getHeight() const;
        float               getDepth() const;

        void                setPosition(const Vec3f &pos);
        void                setPosition(float x, float y, float z = 0.0f);
        const Vec3f        &getPosition() const;

        void                move(const Vec3f &delta);
        void                move(float deltaX, float deltaY, float deltaZ = 0.0f);

        void                setScale(const Vec3f &scale);
        void                setScale(float x, float y, float z = 1.0f);
        const Vec3f        &getScale() const;

        // center of the Sprite. Where its positioned at and rotated at.
        void                setCenter(const Vec3f &center);
        void                setCenter(float x, float y, float z = 0.0f);
        const Vec3f        &getCenter() const;

        void                setRotation(float rotZ);
        void                setRotation(const Vec3f &rot);
        Vec3f               getRotation() const;

        void                setZLevel( float zlevel );
        float               getZLevel() const;

        // whether to draw be by Sprite order or z level.
        // Only works on a per Sprite base.
        void                setDrawSorted( bool drawSorted );
        bool                getDrawSorted() const;

        const Matrix44f    &getTransform() const;
        const Matrix44f    &getInverseTransform() const;
        const Matrix44f    &getGlobalTransform() const;
        const Matrix44f    &getInverseGlobalTransform() const;

        void                addChild( Sprite &child );

        // removes child from Sprite, but does not delete it.
        void                removeChild( Sprite &child );
        // calls removeParent then addChild to parent.
        void                setParent( Sprite *parent );
        // remove child from parent, does not delete.
        void                removeParent();

        // check to see if Sprite contains child
        bool                containsChild( Sprite *child ) const;
        // removes and deletes all children
        void                clearChildren();

        virtual void        setColor( const ci::Color &color );
        virtual void        setColor( float r, float g, float b );
        ci::Color           getColor() const;

        void                setOpacity( float opacity );
        float               getOpacity() const;

        //whether or not to show the entity; does not hide children.
        void                setTransparent(bool transparent);
        bool                getTransparent() const;

        // will show all children that are visible
        void                show();
        // will hide all children as well.
        void                hide();

        bool                visible() const;

        int                 getType() const;

        // removes Sprite from parent and deletes all children. Does not delete Sprite.
        void                remove();

        // check to see if Sprite can be touched
        void                enable(bool flag);
        bool                isEnabled() const;

        Sprite             *getParent() const;

        Vec3f               globalToLocal( const Vec3f &globalPoint );
        Vec3f               localToGlobal( const Vec3f &localPoint );

        // check if a point is inside the Sprite's bounds.
        bool                contains( const Vec3f &point ) const;

        // finds Sprite at position
        Sprite             *getHit( const Vec3f &point );

        void                setProcessTouchCallback( const std::function<void (Sprite *, const TouchInfo &)> &func );
        void                setTapCallback( const std::function<void (Sprite *, const Vec3f &)> &func );
        void                setDoubleTapCallback( const std::function<void (Sprite *, const Vec3f &)> &func );
        void                setDragDestinationCallback( const std::function<void (Sprite *, const DragDestinationInfo &)> &func );

        // Constraints defined in multi_touch_constraints.h
        void                enableMultiTouch(const BitMask &);
        void                disableMultiTouch();
        bool                multiTouchEnabled() const;
        bool                hasMultiTouchConstraint( const BitMask &constraint = MULTITOUCH_NO_CONSTRAINTS ) const;
        bool                multiTouchConstraintNotZero() const;

        bool                inBounds() const;
        void                setCheckBounds(bool checkBounds);
        bool                getCheckBounds() const;
        virtual bool        isLoaded() const;
        void                setDragDestiantion(Sprite *dragDestination);
        Sprite             *getDragDestination() const;

        bool                isDirty() const;
        void                writeAllTo(void* packetClass);

    protected:
        friend class        TouchManager;
        friend class        TouchProcess;
        void                swipe(const Vec3f &swipeVector);
        void                tap(const Vec3f &tapPos);
        void                doubleTap(const Vec3f &tapPos);
        void                dragDestination(Sprite *sprite, const DragDestinationInfo &dragInfo);
        void                processTouchInfo( const TouchInfo &touchInfo );
        void                processTouchInfoCallback( const TouchInfo &touchInfo );
        void                buildTransform() const;
        void                buildGlobalTransform() const;
        virtual void        drawLocalClient();
        virtual void        drawLocalServer();
        bool                hasDoubleTap() const;
        bool                hasTap() const;
        void                setType(int type);
        void                updateCheckBounds() const;
        bool                checkBounds() const;

        virtual void		    markAsDirty(const DirtyState&);
		    // Special function that marks all children as dirty, without sending anything up the hierarchy.
    		virtual void		    markChildrenAsDirty(const DirtyState&);
        virtual void        writeTo(void* packetClass);

        mutable bool        mBoundsNeedChecking;
        mutable bool        mInBounds;


        SpriteEngine       &mEngine;

        float               mWidth;
        float               mHeight;
        float               mDepth;

        mutable Matrix44f   mTransformation;
        mutable Matrix44f   mInverseTransform;
        mutable bool        mUpdateTransform;

        Vec3f               mPosition;
        Vec3f               mCenter;
        Vec3f               mScale;
        Vec3f               mRotation;
        float               mZLevel;
        bool                mDrawSorted;
        float               mOpacity;
        ci::Color           mColor;
        bool                mVisible;
        bool                mTransparent;
        int                 mType;
        bool                mEnabled;

        mutable Matrix44f   mGlobalTransform;
        mutable Matrix44f   mInverseGlobalTransform;

        Sprite             *mParent;
        std::list<Sprite *> mChildren; 

        // Class-unique key for this type.  Subclasses can replace.
        char                mSpriteType;
    		DirtyState			    mDirty;

        std::function<void (Sprite *, const TouchInfo &)> mProcessTouchInfoCallback;
        std::function<void (Sprite *, const Vec3f &)> mSwipeCallback;
        std::function<void (Sprite *, const Vec3f &)> mTapCallback;
        std::function<void (Sprite *, const Vec3f &)> mDoubleTapCallback;
        std::function<void (Sprite *, const DragDestinationInfo &)> mDragDestinationCallback;

        bool                mMultiTouchEnabled;
        BitMask             mMultiTouchConstraints;

        // All touch processing happens in the process touch class
        TouchProcess				mTouchProcess;

        bool                mCheckBounds;

        Sprite             *mDragDestination;
};

} // namespace ui
} // namespace ds

#endif//DS_OBJECT_INTERFACE_H
