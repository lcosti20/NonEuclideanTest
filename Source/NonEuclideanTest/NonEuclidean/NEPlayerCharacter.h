#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "NEPlayerCharacter.generated.h"

class UCameraComponent;
class UInputAction;
class UPortalTraversalComponent;
struct FInputActionValue;

/**
 * First-person player matching the CodeParade NonEuclidean engine's player
 * (GameHeader.h / Player.cpp): no visible body, eye 1.5 m above the feet, 0.2 m radius,
 * 2.9 m/s walk, 60 degree vertical FOV, near-instant acceleration and braking, head bob,
 * no jumping.
 */
UCLASS()
class NONEUCLIDEANTEST_API ANEPlayerCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	ANEPlayerCharacter();

	virtual void Tick(float DeltaSeconds) override;

	UCameraComponent* GetCamera() const { return Camera; }
	UPortalTraversalComponent* GetPortalTraversal() const { return PortalTraversal; }

protected:
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

	void MoveInput(const FInputActionValue& Value);
	void LookInput(const FInputActionValue& Value);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCameraComponent> Camera;

	/** Teleports the player through portals when the camera crosses one. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UPortalTraversalComponent> PortalTraversal;

	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<UInputAction> MoveAction;

	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<UInputAction> LookAction;

	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<UInputAction> MouseLookAction;

	/**
	 * Camera height above the capsule centre (cm), before head bob. The capsule bottom is 85 cm
	 * below the centre and character movement keeps it ~2 cm above the floor, so 63 puts the
	 * eye 1.5 m above the floor, as in the original.
	 */
	UPROPERTY(EditAnywhere, Category = "Camera")
	float EyeOffset = 63.0f;

	/** Enables the original's walking head bob. */
	UPROPERTY(EditAnywhere, Category = "Camera")
	bool bHeadBob = true;

private:
	// Head-bob state, same model as Player::Update/CamOffset in the original.
	float BobMagnitude = 0.0f; // smoothed horizontal speed in m/s (scale-independent)
	float BobPhase = 0.0f;
};
