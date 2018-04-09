<?php

namespace Drupal\flexiform_wizard\WizardStep;

/**
 * Interface for wizard steps plugins.
 */
interface WizardStepInterface {

  /**
   * Get the step informations.
   *
   * @param array $cached_values
   *
   * @return array
   *   Step information including:
   *     - title - The page title.
   *     - form - The fully qualified class name of the form object for this page.
   */
  public function stepInfo($name = '', array $cached_values = []);

}