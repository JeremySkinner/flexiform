<?php
/**
 * @file flexiform.flexiform.inc
 * Helper function for embedding the fields into the flexiform in an arbitrary order
 */

/**
 * Get the base entity for a flexible form.
 *
 * If an id is passed, load the entity and return it. Otherwise create a new
 * entity with the correct type and bundle.
 *
 * @param Flexiform $flexiform
 *   A flexiform object.
 * @param int $base_entity_id
 *   (optional) The id of the base entity if it already exists.
 *
 * @return stdClass|Entity
 *   The base entity for the form.
 */
function flexiform_get_base_entity($flexiform, $base_entity_id = FALSE) {
  if ($base_entity_id) {
    $base_entity = entity_load_single($flexiform->base_entity, $base_entity_id);

    //@todo: Check the entity is the right bundle.
    return $base_entity;
  }

  $info = entity_get_info($flexiform->base_entity);
  $values = array();

  if (!empty($info['entity keys']['bundle'])) {
    $values[$info['entity keys']['bundle']] = $flexiform->base_entity_bundle;
  }

  $base_entity = entity_create($flexiform->base_entity, $values);
  return $base_entity;
}

/**
 * Form callback wrapper: fill out a flexiform.
 *
 * @param $flexiform
 *   The flexiform being submitted by this form.
 * @param $base_entity
 *   The base entity for this form.
 *
 * @see flexiform()
 * @see flexiform_simple()
 */
function flexiform_wrapper($flexiform, $base_entity) {
  $form_id = 'flexiform__' . $flexiform->form;
  return drupal_get_form($form_id, $flexiform, $base_entity);
}

/**
 * Form callback wrapper: flexiform modals.
 *
 * @param $flexiform
 *   The flexiform being wrapped.
 * @param $base_entity
 *   The base entity for this form.
 * @param string $display
 *   The active modal display.
 */
function flexiform_modal_wrapper($flexiform, $base_entity, $display) {
  ctools_include('ajax');
  ctools_include('modal');

  // Prepare form id and form args.
  $form_id = "flexiform__{$flexiform->form}";

  $args = array(
    $flexiform,
    $base_entity
  );

  // Setup form state.
  $form_state = array(
    'ajax' => TRUE,
    'build_info' => array(
      'args' => $args,
    ),
  );
  $commands = ctools_modal_form_wrapper($form_id, $form_state);

  if (!empty($form_state['executed']) && empty($form_state['rebuild'])) {
    $configuration = $flexiform->getDisplay($display)->configuration;

    // The form has been executed, so let's redirect to the destination page.
    $commands = array();
    if (!empty($_GET['destination'])) {
      $commands[] = ctools_ajax_command_redirect($_GET['destination']);
    }
    elseif (!empty($form_state['redirect'])) {
      $commands[] = ctools_ajax_command_redirect($form_state['redirect']);
    }
    elseif ($configuration['ajax_behavior'] == 'modal_dismiss') {
      $commands[] = ctools_modal_command_dismiss();
    }
    else {
      $commands[] = ctools_ajax_command_reload();
    }
  }

  drupal_alter('flexiform_modal_submit_commands', $commands, $flexiform, $form_state);
  ajax_deliver(array(
    '#type' => 'ajax',
    '#commands' => $commands,
  ));
  drupal_exit();
}

/**
 * Build a flexiform.
 *
 * @param array $form
 * @param array $form_state
 * @param Flexiform $flexiform
 * @param Entity|stdClass $base_entity
 * @param bool $include_actions
 *   Boolean flag for whether the actions element should be included at the
 *   bottom of the form.
 *
 * @return
 *   Constructed form array.
 */
function flexiform($form, &$form_state, $flexiform, $base_entity) {
  // @TODO: Consider moving all of these functions into .module easier accessibility.
  form_load_include($form_state, 'inc', 'flexiform', 'includes/flexiform.flexiform');

  $form['#flexiform_builder'] = $builder = $flexiform->getBuilder($base_entity);
  $form = $builder->form($form, $form_state);

  $form['actions'] = array(
    '#type' => 'container',
    '#attributes' => array('class' => array('form-actions')),
    '#weight' => 400,
  );

  // We add the form's #submit array to this button along with the actual submit
  // handler to preserve any submit handlers added by a form callback_wrapper.
  $submit = array();

  if (!empty($form['#submit'])) {
    $submit += $form['#submit'];
  }

  // Get button settings.
  $submit_text = t('Save');
  if (!empty($flexiform->settings['buttons']['submit_text'])) {
    $submit_text = $flexiform->settings['buttons']['submit_text'];
  }

  $form['actions']['submit'] = array(
    '#type' => 'submit',
    '#value' => $submit_text,
    '#submit' => $submit + array('flexiform_submit'),
  );

  if (!empty($flexiform->settings['ajax']['submit'])) {
    $wrapper_id = drupal_html_id($form_state['build_info']['form_id'].'-ajax-wrapper');
    $form['#prefix'] = '<div id="'.$wrapper_id.'">';
    $form['#suffix'] = '</div>';
    $form['actions']['submit']['#ajax'] = array(
      'wrapper' => $wrapper_id,
      'callback' => 'flexiform_submit_ajax',
    );
  }

  // We append the validate handler to #validate in case a form callback_wrapper
  // is used to add validate handlers earlier.
  $form['#validate'][] = 'flexiform_validate';

  if (module_exists('honeypot')) {
    if (!empty($flexiform->settings['honeypot'])) {
      $protections = array_keys(array_filter($flexiform->settings['honeypot']));
      honeypot_add_form_protection($form, $form_state, $protections);
    }
  }

  return $form;
}

/**
 * Validation callback for a flexiform stuff.
 */
function flexiform_validate($form, &$form_state) {
  $form['#flexiform_builder']->formValidate($form, $form_state);
}

/**
 * Submit callback for a flexiform.
 */
function flexiform_submit($form, &$form_state) {
  $form['#flexiform_builder']->formSubmit($form, $form_state);
  $form['#flexiform_builder']->formSubmitRedirect($form, $form_state);
}

/**
 * Submit AJAX callback for a flexiform.
 */
function flexiform_submit_ajax($form, $form_state) {
  $flexiform = $form['#flexiform'];
  if (empty($form_state['executed'])) {
    return $form;
  }

  switch ($flexiform->settings['ajax']['response']) {
    case 'refresh':
      $wrapper = $form_state['triggering_element']['#ajax']['wrapper'];

      // Prevent submitted values bleeding into the new form.
      $original_post = $_POST;
      $_POST = array();
      $new_form = call_user_func_array('drupal_get_form', array_merge(array($form_state['build_info']['form_id']), $form_state['build_info']['args']));
      $_POST = $original_post;
      unset($original_post);

      $commands = array(
        ajax_command_prepend(
          '#'.$wrapper,
          theme('status_messages')
        ),
        ajax_command_replace(
          '#'.$wrapper,
          drupal_render($new_form)
        ),
      );
      break;
    case 'reload':
      ctools_include('ajax');
      $commands = array(
        ctools_ajax_command_reload(),
      );
      break;
    case 'redirect':
      ctools_include('ajax');
      $redirect = !empty($flexiform->settings['ajax']['redirect']) ? $flexiform->settings['ajax']['redirect'] : '';
      $redirect = $form['#flexiform_builder']->replaceCtoolsSubstitutions($redirect);
      $options = array();
      if (!empty($form_state['redirect'])) {
        if (is_array($form_state['redirect'])) {
          $redirect = $form_state['redirect'][0];
          $options = $form_state['redirect'][1];
        }
        else {
          $redirect = $form_state['redirect'];
        }
      }
      $commands = array(
        ctools_ajax_command_redirect($redirect, 0, $options),
      );
      break;
  }

  $context = array(
    'flexiform' => $flexiform,
    'form' => $form,
    'form_state' => $form_state,
  );
  drupal_alter('flexiform_ajax_submit_commands', $commands, $context);

  return array(
    '#type' => 'ajax',
    '#commands' => $commands,
  );
}

/**
 * Add elements for an entity to the form based on settings in the $elements array.
 *
 * @param $entity_namespace
 *  The namespace for this entity in this form
 * @param $flexiform
 *  The flexiform object.
 * @param $entity
 *  The entity object
 * @param $elements array
 *  An array of elementss to be included in the form. Keyed by element_name.
 * @param $form
 * @param $form_state
 */
function flexiform_embed_entity_elements($entity_namespace, $flexiform, $entity, $elements, &$form, &$form_state) {
  // Set up our form bits
  if (!isset($form['#parents'])) {
    $form['#parents'] = array();
  }

  // All the fields for this entity need to end up with the correct parents
  // so set the #parents here to be the entity namespace and unset it after the
  // loop.
  array_push($form['#parents'], $entity_namespace);

  // Iterate over the fields
  foreach ($elements as $element_name => $settings) {
    $element_namespace = FlexiformElement::makeElementNamespace($settings['type'], $entity_namespace, $element_name);
    $element = FlexiformElement::getElement($flexiform, $settings['type'], $element_namespace, $settings);

    $form += $element->form($form, $form_state, $entity);
  }

  array_pop($form['#parents']);
}

/**
 * Prepare the entities needed for the form, either by creating or loading
 *
 * @param $flexiform
 *  The form definition
 * @param $base_entity
 *  The base entity of the form.
 *
 * @return
 *  An array of entities keyed by namespace
 */
function flexiform_prepare_entities($flexiform, $base_entity) {
  $flexiform->getBuilder($base_entity)->prepareEntities();
}

/**
 * Get the namespace we use for the entity
 *
 * @param $entity_type
 *  The entity type e.g. node,user or taxonomy term
 * @param $entity
 *  The entity object
 *
 * @return
 *  A Unique namespace for the entity in this form
 */
function flexiform_get_entity_namespace($entity_type, $entity) {
  $entity_info = entity_get_info($entity_type);
  return $entity_type . '__' . $entity->{$entity_info['entity keys']['id']};
}

/**
 * Get the fields on a form from a particular entity namespace
 *
 * @param $entity_namespace
 *  The namespace as give by flexiform_get_entity_namespace
 * @param $form
 *  Fully Built form object
 *
 * @return
 *  A form array with only the fields from that entity included without the namespace prefixed
 */
function flexiform_get_entity_namespace_fields($entity_namespace, $form) {
  $entity_form = array();
  $namespace_length = strlen($entity_namespace);

  foreach ($form as $key => $field) {
    if (substr($key, 0, 1) == '#') {
      $entity_form[$key] = $field;
    }
    elseif (substr($key, 0, $namespace_length) == $entity_namespace) {
      $entity_form[substr($key, $namespace_length + 2)] = $field;
    }
  }

  $entity_form['#parents'][] = $entity_namespace;

  return $entity_form;
}

/**
 * Get the bundle key for an entity type
 */
function _flexiform_get_bundle_key($entity_type) {
  $info = entity_get_info($entity_type);
  return empty($info['entity keys']['bundle']) ? FALSE : $info['entity keys']['bundle'];
}

/**
 * Get the bundle for an entity type and entity
 */
function _flexiform_get_bundle($entity_type, $entity) {
  $bundle_key = _flexiform_get_bundle_key($entity_type);
  if (!$bundle_key) {
    return $entity_type;
  }
  else {
    return $entity->{$bundle_key};
  }
}

/**
 * Getter callback for 'base entity'
 *
 * @param Flexiform $flexiform
 *   The flexiform object.
 * @param Entity|stdClass $base_entity
 *   The base entity for the form.
 * @param array $entities
 *   An array of entities already embedded in the form.
 * @param string $namespace
 *   The namespace of the entity being got for this form.
 * @param array $config
 *   The settings for this getter, as defined in flexiform_getter_info.
 */
function flexiform_entity_getter_base_entity($flexiform, $base_entity, $entities, $namespace, $config) {
  return $base_entity;
}

/**
 * Getter callback for 'base entity'
 */
function flexiform_entity_setter_base_entity($flexiform, $base_entity, $entity, $entity_type, $entities, $namespace, $config) {
  entity_save($entity_type, $entity);
}
