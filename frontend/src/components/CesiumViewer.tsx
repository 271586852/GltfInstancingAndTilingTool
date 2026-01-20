import React, { useEffect, useRef, useImperativeHandle, forwardRef } from 'react';
import { Viewer, Cesium3DTileset, Ion, Math as CesiumMath } from 'cesium';
import * as Cesium from 'cesium';
import 'cesium/Build/Cesium/Widgets/widgets.css';

// Set your Cesium Ion default access token.
// This is a public token and is safe to be in client-side code.
Ion.defaultAccessToken = 'eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJqdGkiOiI2YWVhMTI3Yi0zMWE3LTRkZjQtOTVkNi1lNDJlYjBkMGRiYzciLCJpZCI6MTU3MTU4LCJpYXQiOjE2OTA2MDYzOTB9.vtUu6fpLw99r3SlKlA_Rkzm2wvx7vWg-aqo-kK5nGDo';

interface CesiumViewerHandles {
  loadTileset: (url: string) => void;
}

const CesiumViewer: React.ForwardRefRenderFunction<CesiumViewerHandles> = (props, ref) => {
  const cesiumContainerRef = useRef<HTMLDivElement>(null);
  const viewerRef = useRef<Viewer | null>(null);

  useEffect(() => {
    if (cesiumContainerRef.current && !viewerRef.current) {
      const viewer = new Viewer(cesiumContainerRef.current, {
        animation: false,
        baseLayerPicker: false,
        fullscreenButton: false,
        geocoder: false,
        homeButton: false,
        infoBox: false,
        sceneModePicker: false,
        selectionIndicator: false,
        timeline: false,
        navigationHelpButton: false,
      });

      // Point the camera to a default view (e.g., looking at a region in China)
      viewer.camera.flyTo({
        destination: new Cesium.Cartesian3(-2183975.565869689, 4384351.46459388, 4075106.398018266),
        orientation: {
          heading: CesiumMath.toRadians(0.0),
          pitch: CesiumMath.toRadians(-25.0),
          roll: 0.0,
        },
      });

      viewerRef.current = viewer;
    }

    return () => {
      // Cleanup viewer on component unmount
      if (viewerRef.current && !viewerRef.current.isDestroyed()) {
        viewerRef.current.destroy();
        viewerRef.current = null;
      }
    };
  }, []); // Empty dependency array ensures this runs only once on mount

  useImperativeHandle(ref, () => ({
    loadTileset: async (url: string) => {
      const viewer = viewerRef.current;
      if (viewer) {
        // Remove all existing tilesets before loading a new one
        viewer.scene.primitives.removeAll();

        try {
          const tileset = await Cesium3DTileset.fromUrl(url);
          viewer.scene.primitives.add(tileset);

          // Zoom to the newly loaded tileset
          viewer.zoomTo(tileset);
        } catch (error) {
          console.error(`Error loading tileset: ${error}`);
          // Optionally, you can have a callback to notify the parent component of the error
        }
      }
    },
  }));

  return <div className="cesium-container" ref={cesiumContainerRef} />;
};

export default forwardRef(CesiumViewer); 