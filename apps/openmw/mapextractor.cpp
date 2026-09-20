#include "mapextractor.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <map>
#include <thread>

#include <osg/ComputeBoundsVisitor>
#include <osg/Group>
#include <osg/Image>
#include <osg/Texture2D>
#include <osgDB/WriteFile>
#include <osgViewer/Viewer>

#include <components/debug/debuglog.hpp>
#include <components/esm3/loadcell.hpp>
#include <components/misc/constants.hpp>
#include <components/misc/convert.hpp>
#include <components/sceneutil/lightmanager.hpp>
#include <components/sceneutil/workqueue.hpp>
#include <components/settings/values.hpp>

#include "mwbase/environment.hpp"
#include "mwbase/mechanicsmanager.hpp"
#include "mwbase/windowmanager.hpp"
#include "mwbase/world.hpp"
#include "mwphysics/collisiontype.hpp"
#include "mwphysics/raycasting.hpp"
#include "mwrender/globalmap.hpp"
#include "mwrender/localmap.hpp"
#include "mwrender/renderingmanager.hpp"
#include "mwrender/vismask.hpp"
#include "mwworld/cellstore.hpp"
#include "mwworld/esmstore.hpp"
#include "mwworld/worldimp.hpp"
#include "mwworld/worldmodel.hpp"

namespace OMW
{
    MapExtractor::MapExtractor(const std::string& worldMapOutput, const std::string& localMapOutput,
        MWRender::RenderingManager* renderingManager, const MWWorld::ESMStore* store, int localMapSize)
        : mWorldMapOutputDir(worldMapOutput)
        , mLocalMapOutputDir(localMapOutput)
        , mRenderingManager(renderingManager)
        , mStore(store)
        , mLocalMap(nullptr)
        , mLocalMapSize(localMapSize)
        , mFitLocalMaps(false)
        , mInteriorDisableWater(false)
        , mInteriorAplhaOption(1)
    {
        // Only create directories if paths are not empty
        if (!mWorldMapOutputDir.empty())
            std::filesystem::create_directories(mWorldMapOutputDir);
        if (!mLocalMapOutputDir.empty())
            std::filesystem::create_directories(mLocalMapOutputDir);

        // Create GlobalMap instance
        if (!mRenderingManager)
        {
            Log(Debug::Error) << "RenderingManager is null in MapExtractor constructor";
            throw std::runtime_error("RenderingManager is null");
        }

        osg::Group* lightRoot = mRenderingManager->getLightRoot();
        if (!lightRoot)
        {
            Log(Debug::Error) << "LightRoot is null in MapExtractor constructor";
            throw std::runtime_error("LightRoot is null");
        }

        osg::Group* root = lightRoot->getParent(0) ? lightRoot->getParent(0)->asGroup() : nullptr;
        if (!root)
        {
            Log(Debug::Error) << "Root node is null in MapExtractor constructor";
            throw std::runtime_error("Root node is null");
        }

        SceneUtil::WorkQueue* workQueue = mRenderingManager->getWorkQueue();
        if (!workQueue)
        {
            Log(Debug::Error) << "WorkQueue is null in MapExtractor constructor";
            throw std::runtime_error("WorkQueue is null");
        }

        try
        {
            mGlobalMap = std::make_unique<MWRender::GlobalMap>(root, workQueue);
            // LocalMap will be set later via setLocalMap() method
            mLocalMap = nullptr;
        }
        catch (const std::exception& e)
        {
            Log(Debug::Error) << "Failed to create map objects: " << e.what();
            throw;
        }
    }

    MapExtractor::~MapExtractor() = default;

    void MapExtractor::extractWorldMap(int cellSize, int borderWidth, bool waterAlphaMode)
    {
        Log(Debug::Info) << "Extracting world map...";

        if (mWorldMapOutputDir.empty())
        {
            Log(Debug::Warning) << "World map output directory is not set, skipping world map extraction";
            return;
        }

        // Create output directory if it doesn't exist
        std::filesystem::create_directories(mWorldMapOutputDir);

        if (!mGlobalMap)
        {
            Log(Debug::Error) << "Global map not initialized";
            throw std::runtime_error("Global map not initialized");
        }

        // Temporarily set cell size for extraction
        const int originalCellSize = Settings::map().mGlobalMapCellSize;
        Settings::map().mGlobalMapCellSize.set(cellSize);

        mGlobalMap->setBorderWidth(borderWidth);
        mGlobalMap->setWaterAlphaMode(waterAlphaMode);
        mGlobalMap->render();
        mGlobalMap->ensureLoaded();

        saveWorldMapTexture();
        saveWorldMapTextureBlocks(waterAlphaMode);
        saveWorldMapInfo();

        // Restore original cell size
        Settings::map().mGlobalMapCellSize.set(originalCellSize);

        Log(Debug::Info) << "World map extraction complete";
    }

    void MapExtractor::saveWorldMapTexture()
    {
        osg::ref_ptr<osg::Texture2D> baseTexture = mGlobalMap->getBaseTexture();
        if (!baseTexture || !baseTexture->getImage())
        {
            Log(Debug::Error) << "Failed to get world map base texture";
            return;
        }

        osg::Image* image = baseTexture->getImage();
        std::filesystem::path outputPath = mWorldMapOutputDir / "map.png";

        if (!osgDB::writeImageFile(*image, outputPath.string()))
        {
            Log(Debug::Error) << "Failed to write world map texture to " << outputPath;
            return;
        }

        Log(Debug::Info) << "Saved world map texture: " << outputPath;
    }

    void MapExtractor::saveWorldMapTextureBlocks(bool waterAlphaMode)
    {
        osg::ref_ptr<osg::Texture2D> baseTexture = mGlobalMap->getBaseTexture();
        if (!baseTexture || !baseTexture->getImage())
        {
            Log(Debug::Error) << "Failed to get world map base texture for block extraction";
            return;
        }

        osg::Image* sourceImage = baseTexture->getImage();
        int imageWidth = sourceImage->s();
        int imageHeight = sourceImage->t();
        int pixelFormat = sourceImage->getPixelFormat();
        int dataType = sourceImage->getDataType();
        int pixelSize = sourceImage->getPixelSizeInBits() / 8;

        // Get grid bounds
        int minX = std::numeric_limits<int>::max();
        int maxX = std::numeric_limits<int>::min();
        int minY = std::numeric_limits<int>::max();
        int maxY = std::numeric_limits<int>::min();

        MWWorld::Store<ESM::Cell>::iterator it = mStore->get<ESM::Cell>().extBegin();
        for (; it != mStore->get<ESM::Cell>().extEnd(); ++it)
        {
            if (it->getGridX() < minX)
                minX = it->getGridX();
            if (it->getGridX() > maxX)
                maxX = it->getGridX();
            if (it->getGridY() < minY)
                minY = it->getGridY();
            if (it->getGridY() > maxY)
                maxY = it->getGridY();
        }

        int pixelsPerCell = Settings::map().mGlobalMapCellSize;
        
        // Calculate block boundaries
        auto getBlockCoord = [](int coord) -> int {
            return coord < 0 ? (coord + 1) / 16 - 1 : coord / 16;
        };

        int minBlockX = getBlockCoord(minX);
        int maxBlockX = getBlockCoord(maxX);
        int minBlockY = getBlockCoord(minY);
        int maxBlockY = getBlockCoord(maxY);

        // Get background color
        osg::Vec3f bgColor = mGlobalMap->getBackgroundColor();
        unsigned char bgR = static_cast<unsigned char>(bgColor.x() * 255);
        unsigned char bgG = static_cast<unsigned char>(bgColor.y() * 255);
        unsigned char bgB = static_cast<unsigned char>(bgColor.z() * 255);
        unsigned char bgA = waterAlphaMode ? 0 : 255;

        Log(Debug::Info) << "Saving world map blocks (" << (maxBlockX - minBlockX + 1) 
                        << "x" << (maxBlockY - minBlockY + 1) << ")...";

        int savedBlocksCount = 0;

        // Process each 16x16 block
        for (int blockX = minBlockX; blockX <= maxBlockX; ++blockX)
        {
            for (int blockY = minBlockY; blockY <= maxBlockY; ++blockY)
            {
                // Calculate block boundaries in cell coordinates
                int blockStartX = blockX * 16;
                int blockStartY = blockY * 16;
                int blockEndX = blockStartX + 16 - 1;
                int blockEndY = blockStartY + 16 - 1;

                // Calculate pixel coordinates in source image
                int pixelStartX = (blockStartX - minX) * pixelsPerCell;
                int pixelStartY = (blockStartY - minY) * pixelsPerCell;
                int blockWidth = 16 * pixelsPerCell;
                int blockHeight = 16 * pixelsPerCell;

                // Create block image
                osg::ref_ptr<osg::Image> blockImage = new osg::Image;
                blockImage->allocateImage(blockWidth, blockHeight, 1, pixelFormat, dataType);

                // Fill with background color
                unsigned char* blockData = blockImage->data();
                for (int y = 0; y < blockHeight; ++y)
                {
                    for (int x = 0; x < blockWidth; ++x)
                    {
                        int idx = (y * blockWidth + x) * pixelSize;
                        blockData[idx] = bgR;
                        blockData[idx + 1] = bgG;
                        blockData[idx + 2] = bgB;
                        if (pixelSize == 4)
                            blockData[idx + 3] = bgA;
                    }
                }

                // Copy relevant portion from source image
                int copyStartX = std::max(0, pixelStartX);
                int copyStartY = std::max(0, pixelStartY);
                int copyEndX = std::min(imageWidth - 1, pixelStartX + blockWidth - 1);
                int copyEndY = std::min(imageHeight - 1, pixelStartY + blockHeight - 1);

                bool hasNonBackgroundPixels = false;

                if (copyStartX <= copyEndX && copyStartY <= copyEndY)
                {
                    for (int srcY = copyStartY; srcY <= copyEndY; ++srcY)
                    {
                        for (int srcX = copyStartX; srcX <= copyEndX; ++srcX)
                        {
                            int destX = srcX - pixelStartX;
                            int destY = srcY - pixelStartY;

                            if (destX >= 0 && destX < blockWidth && destY >= 0 && destY < blockHeight)
                            {
                                unsigned char* srcPixel = sourceImage->data(srcX, srcY);
                                unsigned char* destPixel = blockImage->data(destX, destY);
                                
                                for (int c = 0; c < pixelSize; ++c)
                                {
                                    destPixel[c] = srcPixel[c];
                                }

                                // Check if this pixel differs from background color
                                if (!hasNonBackgroundPixels)
                                {
                                    if (srcPixel[0] != bgR || srcPixel[1] != bgG || srcPixel[2] != bgB)
                                    {
                                        hasNonBackgroundPixels = true;
                                    }
                                }
                            }
                        }
                    }
                }

                // Only save block if it contains non-background pixels
                if (hasNonBackgroundPixels)
                {
                    std::ostringstream filename;
                    filename << "(" << blockX << "," << blockY << ").png";
                    std::filesystem::path outputPath = mWorldMapOutputDir / filename.str();

                    if (!osgDB::writeImageFile(*blockImage, outputPath.string()))
                    {
                        Log(Debug::Warning) << "Failed to write world map block (" << blockX << "," << blockY << ") to " << outputPath;
                    }
                    else
                    {
                        savedBlocksCount++;
                    }
                }
            }
        }

        Log(Debug::Info) << "Saved " << savedBlocksCount << " world map blocks (skipped empty blocks)";
    }

    void MapExtractor::saveWorldMapInfo()
    {
        int width = mGlobalMap->getWidth();
        int height = mGlobalMap->getHeight();

        int minX = std::numeric_limits<int>::max();
        int maxX = std::numeric_limits<int>::min();
        int minY = std::numeric_limits<int>::max();
        int maxY = std::numeric_limits<int>::min();

        MWWorld::Store<ESM::Cell>::iterator it = mStore->get<ESM::Cell>().extBegin();
        for (; it != mStore->get<ESM::Cell>().extEnd(); ++it)
        {
            if (it->getGridX() < minX)
                minX = it->getGridX();
            if (it->getGridX() > maxX)
                maxX = it->getGridX();
            if (it->getGridY() < minY)
                minY = it->getGridY();
            if (it->getGridY() > maxY)
                maxY = it->getGridY();
        }

        std::filesystem::path infoPath = mWorldMapOutputDir / "mapInfo.yaml";
        std::ofstream file(infoPath);

        if (!file)
        {
            Log(Debug::Error) << "Failed to create world map info file: " << infoPath;
            return;
        }

        osg::Vec3f bgColor = mGlobalMap->getBackgroundColor();
        
        // Get current timestamp
        auto now = std::chrono::system_clock::now();
        auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(
            now.time_since_epoch()).count();

        file << "version: 2\n";
        file << "time: " << timestamp << "\n";
        file << "width: " << width << "\n";
        file << "height: " << height << "\n";
        file << "pixelsPerCell: " << Settings::map().mGlobalMapCellSize << "\n";
        file << "waterWithAlpha: " << (mGlobalMap->getWaterAlphaMode() ? "true" : "false") << "\n";
        file << "gridX:\n";
        file << "  min: " << minX << "\n";
        file << "  max: " << maxX << "\n";
        file << "gridY:\n";
        file << "  min: " << minY << "\n";
        file << "  max: " << maxY << "\n";
        file << "file: \"map.png\"\n";
        file << "bColor: [" << bgColor.x() << ", " << bgColor.y() << ", " << bgColor.z() << "]\n";

        file.close();

        Log(Debug::Info) << "Saved world map info: " << infoPath;
    }

    void MapExtractor::extractLocalMaps(const std::vector<const MWWorld::CellStore*>& activeCells)
    {
        Log(Debug::Info) << "Extracting active local maps...";

        if (mLocalMapOutputDir.empty())
        {
            Log(Debug::Warning) << "Local map output directory is not set, skipping local map extraction";
            return;
        }

        // Create output directory if it doesn't exist
        std::filesystem::create_directories(mLocalMapOutputDir);

        const auto& launchParams = MWBase::Environment::get().getLaunchParameters();
        auto dw = launchParams.find("interior-disable-water");
        if (dw != launchParams.end() && !dw->second.empty())
        {
            try
            {
                mInteriorDisableWater = dw->second == "true";
            }
            catch (const std::exception&)
            {
                mInteriorDisableWater = false;
            }
        }

        auto ao = launchParams.find("interior-alpha-option");
        if (ao != launchParams.end() && !ao->second.empty())
        {
            try
            {
                mInteriorAplhaOption = std::stoi(ao->second);
            }
            catch (const std::exception&)
            {
                mInteriorAplhaOption = 2;
            }
            mInteriorAplhaOption = std::clamp(mInteriorAplhaOption, 0, 2);
        }

        // Configure water culling on the LocalMap for interior maps.
        if (mLocalMap && mInteriorDisableWater)
            mLocalMap->setInteriorWaterCulling(mInteriorDisableWater);

        if (!mLocalMap)
        {
            Log(Debug::Error) << "Local map not initialized - cannot extract local maps";
            return;
        }

        Log(Debug::Info) << "LocalMap instance is available, starting extraction";
        
        mFramesToWait = 5;
        
        startExtraction(activeCells);
    }
    
    void MapExtractor::startExtraction(const std::vector<const MWWorld::CellStore*>& activeCells)
    {
        // Enable extraction mode to prevent automatic camera cleanup
        if (mLocalMap)
        {
            mLocalMap->setExtractionMode(true);
        }
        
        Log(Debug::Info) << "Processing " << activeCells.size() << " currently active cells...";

        mPendingExtractions.clear();
        
        for (const MWWorld::CellStore* cellStore : activeCells)
        {
            if (cellStore->getCell()->isExterior())
            {
                int x = cellStore->getCell()->getGridX();
                int y = cellStore->getCell()->getGridY();
                
                std::ostringstream filename;
                filename << "(" << x << "," << y << ").png";
                std::filesystem::path outputPath = mLocalMapOutputDir / filename.str();

                // Check current overwrite flag from World (which checks LaunchParameters)
                bool shouldOverwrite = MWBase::Environment::get().getWorld()->getOverwriteMaps();
                if (!shouldOverwrite && std::filesystem::exists(outputPath))
                {
                    Log(Debug::Info) << "Skipping cell (" << x << "," << y << ") - file already exists";
                    continue;
                }
                
                PendingExtraction extraction;
                extraction.cellStore = cellStore;
                extraction.isExterior = true;
                extraction.outputPath = outputPath;
                extraction.framesWaited = 0;
                mPendingExtractions.push_back(extraction);
            }
            else
            {
                ESM::RefId cellId = cellStore->getCell()->getId();
                std::string cellName(cellStore->getCell()->getNameId());

                std::string lowerCaseId = cellId.toDebugString();
                std::transform(lowerCaseId.begin(), lowerCaseId.end(), lowerCaseId.begin(), ::tolower);

                const std::string invalidChars = "/\\:*?\"<>|";
                lowerCaseId.erase(std::remove_if(lowerCaseId.begin(), lowerCaseId.end(),
                    [&invalidChars](char c) { return invalidChars.find(c) != std::string::npos; }),
                    lowerCaseId.end()
                );

                if (lowerCaseId.empty())
                {
                    lowerCaseId = "_unnamed_cell_";
                }

                std::filesystem::path texturePath = mLocalMapOutputDir / (lowerCaseId + ".png");
                std::filesystem::path yamlPath = mLocalMapOutputDir / (lowerCaseId + ".yaml");

                // Check current overwrite flag from World (which checks LaunchParameters)
                bool shouldOverwrite = MWBase::Environment::get().getWorld()->getOverwriteMaps();
                if (!shouldOverwrite && std::filesystem::exists(yamlPath))
                {
                    Log(Debug::Info) << "Skipping interior cell: " << cellName << " - files already exist";
                    continue;
                }
                
                PendingExtraction extraction;
                extraction.cellStore = cellStore;
                extraction.isExterior = false;
                extraction.outputPath = texturePath;
                extraction.framesWaited = 0;
                mPendingExtractions.push_back(extraction);
            }
        }
        
        if (!mPendingExtractions.empty())
        {
            Log(Debug::Info) << "Queued " << mPendingExtractions.size() << " cells for extraction";
            processNextCell();
        }
        else
        {
            Log(Debug::Info) << "No cells to extract";
            // Clean up any cameras that may have been created
            if (mLocalMap)
            {
                mLocalMap->cleanupCameras();
                mLocalMap->setExtractionMode(false);
            }
        }
    }
    
    void MapExtractor::update()
    {
        if (mPendingExtractions.empty())
            return;
            
        PendingExtraction& current = mPendingExtractions.front();
        current.framesWaited++;
        
        // Wait for the required number of frames before checking
        if (current.framesWaited >= mFramesToWait)
        {
            // Check if the texture is ready before trying to save
            bool textureReady = false;
            
            if (current.isExterior)
            {
                int x = current.cellStore->getCell()->getGridX();
                int y = current.cellStore->getCell()->getGridY();
                
                // Check if the rendered image is ready
                osg::ref_ptr<osg::Image> image = mLocalMap->getMapImage(x, y);
                
                if (image && image->s() > 0 && image->t() > 0 && image->data() != nullptr)
                {
                    textureReady = true;
                }
                else if (current.framesWaited <= 30)
                {
                    return;
                }
            }
            else
            {
                // For interior cells, check if at least one segment has a valid rendered image
                MyGUI::IntRect grid = mLocalMap->getInteriorGrid();
                for (int x = grid.left; x < grid.right && !textureReady; ++x)
                {
                    for (int y = grid.top; y < grid.bottom && !textureReady; ++y)
                    {
                        osg::ref_ptr<osg::Image> image = mLocalMap->getMapImage(x, y);
                        if (image && image->s() > 0 && image->t() > 0 && image->data() != nullptr)
                        {
                            textureReady = true;
                        }
                    }
                }
                
                
                if (!textureReady && current.framesWaited <= 30)
                {
                    return;
                }
            }
            
            if (textureReady)
            {
                savePendingExtraction(current);
                
                mPendingExtractions.erase(mPendingExtractions.begin());
                
                // Clean up the camera for the current cell immediately after saving
                // This prevents memory buildup when processing many cells
                if (mLocalMap)
                    mLocalMap->cleanupCameras();
                
                if (!mPendingExtractions.empty())
                {
                    processNextCell();
                }
                else
                {
                    // Disable extraction mode
                    mLocalMap->setExtractionMode(false);
                    Log(Debug::Info) << "Extraction of active local maps complete";
                }
            }
            else if (current.framesWaited > 30)
            {
                // If we've waited too long (30 frames = ~0.5 seconds at 60 fps), skip this cell
                if (current.isExterior)
                {
                    int x = current.cellStore->getCell()->getGridX();
                    int y = current.cellStore->getCell()->getGridY();
                    Log(Debug::Warning) << "Timeout waiting for texture for cell (" << x << "," << y << "), skipping";
                }
                else
                {
                    std::string cellName(current.cellStore->getCell()->getNameId());
                    Log(Debug::Warning) << "Timeout waiting for texture for interior cell: " << cellName << ", skipping";
                }
                
                mPendingExtractions.erase(mPendingExtractions.begin());
                
                // Clean up cameras even after timeout
                if (mLocalMap)
                    mLocalMap->cleanupCameras();
                
                if (!mPendingExtractions.empty())
                {
                    processNextCell();
                }
                else
                {
                    // Disable extraction mode
                    mLocalMap->setExtractionMode(false);
                    Log(Debug::Info) << "Extraction of active local maps complete";
                }
            }
            // Otherwise keep waiting for the texture to be ready
        }
    }
    
    bool MapExtractor::isExtractionComplete() const
    {
        return mPendingExtractions.empty();
    }
    
    void MapExtractor::processNextCell()
    {
        if (mPendingExtractions.empty())
            return;
            
        const PendingExtraction& extraction = mPendingExtractions.front();
        
        if (extraction.isExterior)
        {
            int x = extraction.cellStore->getCell()->getGridX();
            int y = extraction.cellStore->getCell()->getGridY();
            mLocalMap->clearCellCache(x, y);
            mLocalMap->requestMap(const_cast<MWWorld::CellStore*>(extraction.cellStore));
        }
        else
        {
            if (mFitLocalMaps)
                mLocalMap->requestInteriorMapFitted(const_cast<MWWorld::CellStore*>(extraction.cellStore));
            else
                mLocalMap->requestMap(const_cast<MWWorld::CellStore*>(extraction.cellStore));
        }
    }
    
    bool MapExtractor::savePendingExtraction(const PendingExtraction& extraction)
    {
        // Get current overwrite flag from World (which checks LaunchParameters)
        bool shouldOverwrite = MWBase::Environment::get().getWorld()->getOverwriteMaps();
        
        if (extraction.isExterior)
        {
            return extractExteriorCell(extraction.cellStore, shouldOverwrite);
        }
        else
        {
            return extractInteriorCell(extraction.cellStore, shouldOverwrite);
        }
    }

    bool MapExtractor::extractExteriorCell(const MWWorld::CellStore* cellStore, bool forceOverwrite)
    {
        int x = cellStore->getCell()->getGridX();
        int y = cellStore->getCell()->getGridY();

        std::ostringstream filename;
        filename << "(" << x << "," << y << ").png";
        std::filesystem::path outputPath = mLocalMapOutputDir / filename.str();

        osg::ref_ptr<osg::Image> image = mLocalMap->getMapImage(x, y);
        
        if (!image)
        {
            Log(Debug::Warning) << "Texture for cell (" << x << "," << y << ") has no image data attached";
            return false;
        }

        if (image->s() == 0 || image->t() == 0)
        {
            Log(Debug::Warning) << "Empty image for cell (" << x << "," << y << ") - size: " 
                               << image->s() << "x" << image->t();
            return false;
        }

        osg::ref_ptr<osg::Image> outputImage = new osg::Image(*image, osg::CopyOp::DEEP_COPY_ALL);
        
        if (outputImage->s() != mLocalMapSize || outputImage->t() != mLocalMapSize)
        {
            outputImage->scaleImage(mLocalMapSize, mLocalMapSize, 1);
        }
        if (osgDB::writeImageFile(*outputImage, outputPath.string()))
        {
            Log(Debug::Info) << "Successfully saved local map for cell (" << x << "," << y << ") to " << outputPath;
            saveExteriorCellHeights(cellStore);
            return true;
        }
        else
        {
            Log(Debug::Warning) << "Failed to write texture for cell (" << x << "," << y << ") to " << outputPath;
            return false;
        }
    }

    void MapExtractor::saveExteriorCellHeights(const MWWorld::CellStore* cellStore)
    {
        // Only save heights if water alpha mode is enabled, since heights are only relevant for tilemap water transparency
        const auto& launchParams = MWBase::Environment::get().getLaunchParameters();
        auto it = launchParams.find("tilemap-downscale-factor");
        if (it != launchParams.end() && it->second == "-1")
            return;
        it = launchParams.find("world-map-disable-water-alpha");
        if (it != launchParams.end() && it->second == "true")
            return;

        const MWPhysics::RayCastingInterface* rayCasting = MWBase::Environment::get().getWorld()->getRayCasting();
        if (!rayCasting)
            return;

        const int x = cellStore->getCell()->getGridX();
        const int y = cellStore->getCell()->getGridY();

        constexpr int gridSize = 65;
        constexpr float step = static_cast<float>(Constants::CellSizeInUnits) / static_cast<float>(gridSize - 1);
        constexpr float rayTop = 20000.0f;
        constexpr float rayBottom = -10000.0f;

        std::vector<float> heights(gridSize * gridSize);
        const int mask = MWPhysics::CollisionType_World | MWPhysics::CollisionType_HeightMap
            | MWPhysics::CollisionType_Door;

        for (int gy = 0; gy < gridSize; ++gy)
        {
            for (int gx = 0; gx < gridSize; ++gx)
            {
                const float worldX = x * Constants::CellSizeInUnits + gx * step;
                const float worldY = y * Constants::CellSizeInUnits + gy * step;

                const MWPhysics::RayCastingResult result = rayCasting->castRay(
                    osg::Vec3f(worldX, worldY, rayTop), osg::Vec3f(worldX, worldY, rayBottom), mask);

                heights[gy * gridSize + gx] = result.mHit ? result.mHitPos.z() : rayBottom;
            }
        }

        std::ostringstream filename;
        filename << "(" << x << "," << y << ").heights";
        std::filesystem::path outputPath = mLocalMapOutputDir / filename.str();

        std::ofstream file(outputPath, std::ios::binary | std::ios::trunc);
        if (!file)
        {
            Log(Debug::Warning) << "Failed to create heights file: " << outputPath;
            return;
        }

        file.write(reinterpret_cast<const char*>(heights.data()),
            static_cast<std::streamsize>(heights.size() * sizeof(float)));
        file.close();

        if (!file)
            Log(Debug::Warning) << "Failed to write heights file: " << outputPath;
    }

    bool MapExtractor::extractInteriorCell(const MWWorld::CellStore* cellStore, bool forceOverwrite)
    {
        ESM::RefId cellId = cellStore->getCell()->getId();
        std::string cellName(cellStore->getCell()->getNameId());

        Log(Debug::Info) << "Saving interior cell: " << cellName;

        saveInteriorCellTextures(cellId, cellName);
        return true;
    }

    void MapExtractor::saveInteriorCellTextures(const ESM::RefId& cellId, const std::string& cellName)
    {
        bool useAlpha = mInteriorAplhaOption != 0;
        int pixelSize = useAlpha ? 4 : 3;
        MyGUI::IntRect grid = mLocalMap->getInteriorGrid();
        
        std::string lowerCaseId = cellId.toDebugString();
        std::transform(lowerCaseId.begin(), lowerCaseId.end(), lowerCaseId.begin(), ::tolower);

        const std::string invalidChars = "/\\:*?\"<>|";
        lowerCaseId.erase(std::remove_if(lowerCaseId.begin(), lowerCaseId.end(),
            [&invalidChars](char c) { return invalidChars.find(c) != std::string::npos; }),
            lowerCaseId.end()
        );

        if (lowerCaseId.empty())
        {
            lowerCaseId = "_unnamed_cell_";
        }

        int minX = grid.right;
        int maxX = grid.left;
        int minY = grid.bottom;
        int maxY = grid.top;
        
        // Cache images and find bounds
        std::map<std::pair<int, int>, osg::ref_ptr<osg::Image>> imageCache;
        
        for (int x = grid.left; x < grid.right; ++x)
        {
            for (int y = grid.top; y < grid.bottom; ++y)
            {
                // Get the rendered image directly from camera attachment
                osg::ref_ptr<osg::Image> image = mLocalMap->getMapImage(x, y);
                if (image && image->s() > 0 && image->t() > 0 && image->data() != nullptr)
                {
                    if (image->s() != mLocalMapSize || image->t() != mLocalMapSize)
                    {
                        image->scaleImage(mLocalMapSize, mLocalMapSize, 1);
                    }

                    imageCache[{x, y}] = image;
                    minX = std::min(minX, x);
                    maxX = std::max(maxX, x);
                    minY = std::min(minY, y);
                    maxY = std::max(maxY, y);
                }
            }
        }
        
        if (minX > maxX || minY > maxY)
        {
            Log(Debug::Warning) << "No valid image segments found for interior cell: " << cellName;
            return;
        }
        
        int segmentsX = maxX - minX + 1;
        int segmentsY = maxY - minY + 1;
        
        if (segmentsX <= 0 || segmentsY <= 0)
        {
            Log(Debug::Warning) << "Invalid segment dimensions for interior cell: " << cellName;
            return;
        }

        int totalWidth = segmentsX * mLocalMapSize;
        int totalHeight = segmentsY * mLocalMapSize;
        
        osg::ref_ptr<osg::Image> combinedImage = new osg::Image;
        combinedImage->allocateImage(totalWidth, totalHeight, 1, useAlpha ? GL_RGBA : GL_RGB, GL_UNSIGNED_BYTE);
        
        unsigned char* data = combinedImage->data();
        memset(data, 0, totalWidth * totalHeight * pixelSize);

        for (int x = minX; x <= maxX; ++x)
        {
            for (int y = minY; y <= maxY; ++y)
            {
                auto it = imageCache.find({x, y});
                if (it == imageCache.end())
                    continue;

                osg::ref_ptr<osg::Image> segmentImage = it->second;
                int segWidth = segmentImage->s();
                int segHeight = segmentImage->t();
                
                int destX = (x - minX) * mLocalMapSize;
                int destY = (y - minY) * mLocalMapSize;

                for (int sy = 0; sy < std::min(segHeight, mLocalMapSize); ++sy)
                {
                    for (int sx = 0; sx < std::min(segWidth, mLocalMapSize); ++sx)
                    {
                        unsigned char* srcPixel = segmentImage->data(sx, sy);
                        int dx = destX + sx;
                        int dy = destY + sy;
                        
                        if (dx < totalWidth && dy < totalHeight)
                        {
                            unsigned char* destPixel = data + ((dy * totalWidth + dx) * pixelSize);
                            destPixel[0] = srcPixel[0];
                            destPixel[1] = srcPixel[1];
                            destPixel[2] = srcPixel[2];
                            if (useAlpha)
                                destPixel[3] = 255;
                        }
                    }
                }
            }
        }

        // Fade the empty border of interiors to black (option 1).
        // Interior maps have a 500 game-unit border around the content. Water can
        // render into this border, so we smoothly fade it out starting at the content
        // edge (500 units) down to a reserved 50 units from the texture edge.
        if (mInteriorAplhaOption == 1)
        {
            const float mapWorldSize = mLocalMap->getEffectiveMapWorldSize();
            if (mapWorldSize > 0.f)
            {
                const float pixelsPerUnit = static_cast<float>(mLocalMapSize) / mapWorldSize;
                const float fadeStartPx = 500.0f * pixelsPerUnit; // distance from edge where fade begins
                const float fadeEndPx = 50.0f * pixelsPerUnit;    // distance from edge where fully black
                const float fadeRange = fadeStartPx - fadeEndPx;

                if (fadeRange > 0.f)
                {
                    // Use a rounded-rectangle distance field so that the fade follows a smooth
                    // rounded contour around the corners (avoiding the diagonal "beam" produced
                    // by a clamped axis-aligned distance).
                    const float halfW = static_cast<float>(totalWidth) * 0.5f;
                    const float halfH = static_cast<float>(totalHeight) * 0.5f;
                    const float cornerRadius = std::min(fadeStartPx, std::min(halfW, halfH));

                    for (int y = 0; y < totalHeight; ++y)
                    {
                        const float py = static_cast<float>(y) + 0.5f - halfH;
                        for (int x = 0; x < totalWidth; ++x)
                        {
                            const float px = static_cast<float>(x) + 0.5f - halfW;

                            // Rounded-box signed distance; negated to yield distance from the edge.
                            const float qx = std::abs(px) - (halfW - cornerRadius);
                            const float qy = std::abs(py) - (halfH - cornerRadius);
                            const float qx0 = std::max(qx, 0.0f);
                            const float qy0 = std::max(qy, 0.0f);
                            const float distPx = cornerRadius
                                - (std::sqrt(qx0 * qx0 + qy0 * qy0) + std::min(std::max(qx, qy), 0.0f));

                            float factor;
                            if (distPx <= fadeEndPx)
                                factor = 0.0f;
                            else if (distPx >= fadeStartPx)
                                factor = 1.0f;
                            else
                                factor = (distPx - fadeEndPx) / fadeRange;

                            unsigned char* p = data + ((y * totalWidth + x) * pixelSize);
                            if (!useAlpha)
                            {
                                p[0] = static_cast<unsigned char>(p[0] * factor);
                                p[1] = static_cast<unsigned char>(p[1] * factor);
                                p[2] = static_cast<unsigned char>(p[2] * factor);
                            }
                            else
                            {
                                p[3] = static_cast<unsigned char>(255 * factor);
                            }
                        }
                    }
                }
            }
        }
        // Raycast the cell and blacken areas without solid geometry (option 3).
        // A vertical ray is cast downward for each sample. Where it misses, the
        // texture fades to black with a smooth falloff over two step sizes, so the
        // transition has no hard square edges.
        else if (mInteriorAplhaOption == 2)
        {
            const MWPhysics::RayCastingInterface* rayCasting
                = MWBase::Environment::get().getWorld()->getRayCasting();
            if (rayCasting)
            {
                const osg::BoundingBox& bounds = mLocalMap->getInteriorBounds();
                const osg::Vec2f& center = mLocalMap->getInteriorCenter();
                const float angle = mLocalMap->getInteriorAngle();
                const float mapWorldSize = mLocalMap->getEffectiveMapWorldSize();

                if (mapWorldSize > 0.f)
                {
                    const float pixelsPerUnit = static_cast<float>(mLocalMapSize) / mapWorldSize;

                    // Use the same ray step as exterior maps (128 game units).
                    constexpr float rayStep
                        = static_cast<float>(Constants::CellSizeInUnits) / 128.f;
                    float rayTop = bounds.zMax() + 2000.0f;
                    float rayBottom = bounds.zMin() - 2000.0f;
                    const int mask = MWPhysics::CollisionType_World | MWPhysics::CollisionType_HeightMap
                        | MWPhysics::CollisionType_Door;

                    const float minX = bounds.xMin();
                    const float minY = bounds.yMin();
                    const float widthUnits = bounds.xMax() - bounds.xMin();
                    const float heightUnits = bounds.yMax() - bounds.yMin();

                    const int gridW = std::max(1, static_cast<int>(std::ceil(widthUnits / rayStep)));
                    const int gridH = std::max(1, static_cast<int>(std::ceil(heightUnits / rayStep)));

                    // Size in pixels covered by a single ray sample.
                    const float blockPx = rayStep * pixelsPerUnit;

                    // Inverse of the render rotation: rotated frame -> world frame.
                    const float ca = std::cos(angle);
                    const float sa = std::sin(angle);
                    auto toWorld = [&](float rx, float ry) -> osg::Vec2f {
                        const float dx = rx - center.x();
                        const float dy = ry - center.y();
                        return osg::Vec2f(ca * dx + sa * dy + center.x(), -sa * dx + ca * dy + center.y());
                    };

                    // 1) Cast rays, building a per-sample hit mask.
                    std::vector<bool> hit(static_cast<size_t>(gridW) * gridH, false);
                    for (int gy = 0; gy < gridH; ++gy)
                    {
                        for (int gx = 0; gx < gridW; ++gx)
                        {
                            const float rx = minX + (gx + 0.5f) * rayStep;
                            const float ry = minY + (gy + 0.5f) * rayStep;
                            const osg::Vec2f world = toWorld(rx, ry);
                            hit[gy * gridW + gx] = rayCasting
                                ->castRay(osg::Vec3f(world.x(), world.y(), rayTop),
                                    osg::Vec3f(world.x(), world.y(), rayBottom), mask)
                                .mHit;
                        }
                    }

                    // 2) Chamfer distance transform: distance (in grid cells) from each
                    // sample to the nearest "hit" sample.
                    const float inf = std::numeric_limits<float>::infinity();
                    constexpr float diag = 1.41421356f;
                    std::vector<float> dist(static_cast<size_t>(gridW) * gridH, inf);
                    for (int i = 0; i < gridW * gridH; ++i)
                        if (hit[i])
                            dist[i] = 0.f;

                    // Forward pass
                    for (int gy = 0; gy < gridH; ++gy)
                    {
                        for (int gx = 0; gx < gridW; ++gx)
                        {
                            const int i = gy * gridW + gx;
                            if (hit[i])
                                continue;
                            float d = dist[i];
                            if (gy > 0)
                            {
                                d = std::min(d, dist[i - gridW] + 1.f);
                                if (gx > 0)
                                    d = std::min(d, dist[i - gridW - 1] + diag);
                                if (gx < gridW - 1)
                                    d = std::min(d, dist[i - gridW + 1] + diag);
                            }
                            if (gx > 0)
                                d = std::min(d, dist[i - 1] + 1.f);
                            dist[i] = d;
                        }
                    }
                    // Backward pass
                    for (int gy = gridH - 1; gy >= 0; --gy)
                    {
                        for (int gx = gridW - 1; gx >= 0; --gx)
                        {
                            const int i = gy * gridW + gx;
                            if (hit[i])
                                continue;
                            float d = dist[i];
                            if (gy < gridH - 1)
                            {
                                d = std::min(d, dist[i + gridW] + 1.f);
                                if (gx > 0)
                                    d = std::min(d, dist[i + gridW - 1] + diag);
                                if (gx < gridW - 1)
                                    d = std::min(d, dist[i + gridW + 1] + diag);
                            }
                            if (gx < gridW - 1)
                                d = std::min(d, dist[i + 1] + 1.f);
                            dist[i] = d;
                        }
                    }

                    // 3) Convert distance to a per-sample fade factor: 1 for hits,
                    // fading to 0 over two step sizes beyond the nearest hit.
                    constexpr float fadeCells = 4.f;
                    std::vector<float> factor(static_cast<size_t>(gridW) * gridH, 0.f);
                    for (int i = 0; i < gridW * gridH; ++i)
                        factor[i] = hit[i] ? 1.f : std::clamp(1.f - dist[i] / fadeCells, 0.f, 1.f);

                    // 4) Bilinearly sample the factor field per pixel and apply it,
                    // avoiding blocky squares and hard boundaries.
                    for (int py = 0; py < totalHeight; ++py)
                    {
                        float v = static_cast<float>(py) / blockPx - 0.5f;
                        v = std::clamp(v, 0.f, static_cast<float>(gridH) - 1.f);
                        const int j0 = static_cast<int>(std::floor(v));
                        const int j1 = std::min(j0 + 1, gridH - 1);
                        const float fv = v - static_cast<float>(j0);

                        for (int px = 0; px < totalWidth; ++px)
                        {
                            float u = static_cast<float>(px) / blockPx - 0.5f;
                            u = std::clamp(u, 0.f, static_cast<float>(gridW) - 1.f);
                            const int i0 = static_cast<int>(std::floor(u));
                            const int i1 = std::min(i0 + 1, gridW - 1);
                            const float fu = u - static_cast<float>(i0);

                            const float f00 = factor[j0 * gridW + i0];
                            const float f10 = factor[j0 * gridW + i1];
                            const float f01 = factor[j1 * gridW + i0];
                            const float f11 = factor[j1 * gridW + i1];

                            const float f
                                = (f00 * (1.f - fu) + f10 * fu) * (1.f - fv) + (f01 * (1.f - fu) + f11 * fu) * fv;

                            unsigned char* p = data + ((py * totalWidth + px) * pixelSize);
                            if (!useAlpha)
                            {
                                p[0] = static_cast<unsigned char>(p[0] * f);
                                p[1] = static_cast<unsigned char>(p[1] * f);
                                p[2] = static_cast<unsigned char>(p[2] * f);
                            }
                            else
                            {
                                p[3] = static_cast<unsigned char>(255 * f);
                            }
                        }
                    }
                }
            }
        }

        std::filesystem::path texturePath = mLocalMapOutputDir / (lowerCaseId + ".png");
        
        if (osgDB::writeImageFile(*combinedImage, texturePath.string()))
        {
            Log(Debug::Info) << "Successfully saved interior map to " << texturePath;
        }
        else
        {
            Log(Debug::Error) << "Failed to write interior map to " << texturePath;
            return;
        }

        saveInteriorMapInfo(cellId, lowerCaseId, segmentsX, segmentsY, useAlpha);
    }

    void MapExtractor::saveInteriorMapInfo(const ESM::RefId& cellId, const std::string& lowerCaseId,
                                           int segmentsX, int segmentsY, bool hasAlpha)
    {
        // Get the bounds, center and angle that LocalMap actually used for rendering
        const osg::BoundingBox& bounds = mLocalMap->getInteriorBounds();
        const osg::Vec2f& center = mLocalMap->getInteriorCenter();
        const float nA = mLocalMap->getInteriorAngle();
        
        osg::Vec2f min(bounds.xMin(), bounds.yMin());
        
        // Use the effective map world size
        const float mapWorldSize = mLocalMap->getEffectiveMapWorldSize();
        
        // Calculate position of world origin (0,0) on the rotated map
        osg::Vec2f toOrigin(0.0f - center.x(), 0.0f - center.y());
        float rotatedX = toOrigin.x() * std::cos(nA) - toOrigin.y() * std::sin(nA) + center.x();
        float rotatedY = toOrigin.x() * std::sin(nA) + toOrigin.y() * std::cos(nA) + center.y();
        
        // Convert to texture coordinates (pixels from bottom-left corner)
        float oX = (rotatedX - min.x()) / mapWorldSize * static_cast<float>(mLocalMapSize);
        float oY = (rotatedY - min.y()) / mapWorldSize * static_cast<float>(mLocalMapSize);
        
        float totalHeight = segmentsY * static_cast<float>(mLocalMapSize);

        std::filesystem::path yamlPath = mLocalMapOutputDir / (lowerCaseId + ".yaml");
        std::ofstream file(yamlPath);

        if (!file)
        {
            Log(Debug::Error) << "Failed to create interior map info file: " << yamlPath;
            return;
        }

        // TODO: Replace hardcoded padding with a constant from LocalMap
        // Padding value taken from the implementation in localmap.cpp.
        // Used to shrink the bounds to match the one used during cell rendering.
        const float padding = 500.0f;

        // Scale factor: ratio of default mapWorldSize to effective mapWorldSize
        const float defaultMapWorldSize = static_cast<float>(mLocalMap->getMapWorldSize());
        const float tSc = defaultMapWorldSize / mapWorldSize;

        file << "v: 2\n";
        file << "nA: " << nA << "\n";
        file << "oX: " << oX << "\n";
        file << "oY: " << oY << "\n";
        file << "wT: " << segmentsX << "\n";
        file << "hT: " << segmentsY << "\n";
        file << "tSc: " << tSc << "\n";
        file << "tS: " << mLocalMapSize << "\n";
        if (hasAlpha)
            file << "hA: " << "true" << "\n";
        file << "mBnds:\n";
        file << "  min: [" << bounds.xMin() + padding << ", " << bounds.yMin() + padding << ", " << bounds.zMin() << "]\n";
        file << "  max: [" << bounds.xMax() - padding << ", " << bounds.yMax() - padding << ", " << bounds.zMax() << "]\n";

        file.close();
    }
}
